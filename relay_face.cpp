// RELAY native X11 face. No image assets or graphics toolkit required.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <cstdint>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
using Clock=std::chrono::steady_clock;
static int64_t ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
static int clamp(int n,int lo,int hi){return std::max(lo,std::min(hi,n));}
// Read switch states without grabbing the keyboard.
struct Hinge {
    int fd=-1,tablet=-1,lid=-1;
    int64_t retry=0;
    ~Hinge(){if(fd>=0)close(fd);}
    void update(int64_t now){
        if(now<retry)return;
        retry=now+(fd<0?5000:250);
        if(fd<0){
            for(int i=0;i<64;i++){
                std::string p="/dev/input/event"+std::to_string(i);
                int n=open(p.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
                if(n<0)continue;
                char name[128]={};unsigned long supported=0;
                if(ioctl(n,EVIOCGNAME(sizeof(name)),name)>=0 &&
                   std::string(name)=="brain-kbd-i2c" &&
                   ioctl(n,EVIOCGBIT(EV_SW,sizeof(supported)),&supported)>=0 &&
                   (supported&(1UL<<SW_TABLET_MODE))){fd=n;break;}
                close(n);
            }
            if(fd<0)return;
        }
        unsigned long bits=0;
        if(ioctl(fd,EVIOCGSW(sizeof(bits)),&bits)<0){
            close(fd);fd=-1;tablet=lid=-1;return;
        }
        tablet=(bits>>SW_TABLET_MODE)&1;
        lid=(bits>>SW_LID)&1;
        struct input_event discard[32];
        for(int i=0;i<4&&read(fd,discard,sizeof(discard))>0;i++){}
    }
};
struct Child {
    pid_t pid=-1; int fd=-1; bool received=false; std::string reply;
    bool active() const {return pid>0;}
    bool start(const std::string& message){
        if(active())return false;
        int p[2]; if(pipe(p)<0)return false;
        std::string prompt="User: "+message+"\nBot:";
        std::string seed=std::to_string(std::time(nullptr));
        pid=fork();
        if(pid==0){
            close(p[0]); dup2(p[1],STDOUT_FILENO); close(p[1]);
            int log=open(".relay-debug.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
            if(log>=0){dup2(log,STDERR_FILENO);close(log);}
            execl("./tinytalk","tinytalk","model_neo_q4.bin","tok_neo.bin",prompt.c_str(),
                  "--max","20","--temp","0.65","--top-p","0.9","--kv","72","--seed",seed.c_str(),(char*)nullptr);
            _exit(127);
        }
        close(p[1]); if(pid<0){close(p[0]);return false;}
        fd=p[0]; fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);
        reply.clear();received=false;return true;
    }
    void update(){
        if(!active())return;
        char b[256]; ssize_t n;
        while((n=read(fd,b,sizeof b))>0){
            received=true;
            for(ssize_t i=0;i<n;i++){
                unsigned char ch=b[i];
                if(reply.size()<1024)reply+=(ch>=32&&ch<127)?char(ch):' ';
            }
        }
        int status=0; pid_t done=waitpid(pid,&status,WNOHANG);
        if(done==pid|| (done<0&&errno==ECHILD)){
            close(fd);fd=-1;pid=-1;
            if(done>0&&(!WIFEXITED(status)||WEXITSTATUS(status)!=0))reply+=" [model error]";
            if(reply.empty())reply="No reply. Try a shorter question.";
        }
    }
    void stop(){
        if(active()){
            kill(pid,SIGKILL);while(waitpid(pid,nullptr,0)<0&&errno==EINTR){}
            close(fd);fd=-1;pid=-1;
        }
    }
    ~Child(){stop();}
};
struct Face {
    Display* d; Window win; GC gc; Pixmap pix=0; XFontStruct* font;
    int w=480,h=272; int depth; bool fullscreen=false;
    int scale=1024,offsetX=0,offsetY=0;
    int fontW=8,fontH=16,fontAscent=12;
    Pixmap fontAtlas=0;
    struct TouchKey {int x,y,w,h;std::string action;};
    std::vector<TouchKey> keys;
    int pupilX[2]={0,0},pupilY[2]={0,0},pointerX=-1,pointerY=-1;
    int64_t pointerMoved=0;
    unsigned long black,outer,outline,inner,green,highlight,muted;
    Face(Display* display):d(display){
        int screen=DefaultScreen(d);depth=DefaultDepth(d,screen);
        // Fit the actual desktop, including on the smaller Sharp display.
        int sw=DisplayWidth(d,screen),sh=DisplayHeight(d,screen);
        w=std::min(640,std::max(1,sw-12));h=std::min(360,std::max(1,sh-42));
        win=XCreateSimpleWindow(d,RootWindow(d,screen),(sw-w)/2,(sh-h)/2,w,h,0,0,0);
        XSizeHints hints{};hints.flags=PPosition|PSize;hints.x=(sw-w)/2;hints.y=(sh-h)/2;hints.width=w;hints.height=h;
        XSetWMNormalHints(d,win,&hints);
        XStoreName(d,win,"RELAY");gc=XCreateGC(d,win,0,nullptr);
        font=nullptr;
        for(const char* name:{"10x20","9x18","9x15","fixed"}){
            font=XLoadQueryFont(d,name);if(font)break;
        }
        if(font){
            XSetFont(d,gc,font->fid);
            fontW=std::max(1,int(font->max_bounds.width));
            fontH=std::max(1,font->ascent+font->descent);
            fontAscent=font->ascent;
            if(fontH<15)makeLargeFont();
        }
        black=color("#030604");outer=color("#0a2115");outline=color("#216d40");
        inner=color("#164e2e");green=color("#57ee91");highlight=color("#c0ffda");muted=color("#82b997");
        XSelectInput(d,win,ExposureMask|KeyPressMask|ButtonPressMask|ButtonReleaseMask|StructureNotifyMask);
        // Fullscreen from launch so the face is centered on the physical LCD.
        Atom state=XInternAtom(d,"_NET_WM_STATE",False),full=XInternAtom(d,"_NET_WM_STATE_FULLSCREEN",False);
        XChangeProperty(d,win,state,XA_ATOM,32,PropModeReplace,reinterpret_cast<unsigned char*>(&full),1);
        fullscreen=true;
        XMapWindow(d,win); resize(w,h);
    }
    ~Face(){if(pix)XFreePixmap(d,pix);if(fontAtlas)XFreePixmap(d,fontAtlas);if(font)XFreeFont(d,font);XFreeGC(d,gc);XDestroyWindow(d,win);}
    // X11 installations with only the tiny "fixed" font get a cached 2x
    // bitmap atlas. No font packages or per-frame glyph scaling are needed.
    void makeLargeFont(){
        const int count=95,fw=fontW,fh=fontH;
        Pixmap src=XCreatePixmap(d,win,fw*count,fh,1);
        GC sg=XCreateGC(d,src,0,nullptr);
        XSetForeground(d,sg,0);XFillRectangle(d,src,sg,0,0,fw*count,fh);
        XSetForeground(d,sg,1);XSetFont(d,sg,font->fid);
        for(int i=0;i<count;i++){char ch=char(i+32);XDrawString(d,src,sg,i*fw,fontAscent,&ch,1);}
        XImage* img=XGetImage(d,src,0,0,fw*count,fh,1,ZPixmap);
        if(img){
            fontAtlas=XCreatePixmap(d,win,fw*count*2,fh*2,1);
            GC ag=XCreateGC(d,fontAtlas,0,nullptr);
            XSetForeground(d,ag,0);XFillRectangle(d,fontAtlas,ag,0,0,fw*count*2,fh*2);
            XSetForeground(d,ag,1);
            std::vector<XRectangle> runs;
            for(int y=0;y<fh;y++)for(int x=0;x<fw*count;x++){
                if(XGetPixel(img,x,y))runs.push_back(XRectangle{short(x*2),short(y*2),2,2});
            }
            if(!runs.empty())XFillRectangles(d,fontAtlas,ag,runs.data(),int(runs.size()));
            XFreeGC(d,ag);XDestroyImage(img);
            fontW*=2;fontH*=2;fontAscent*=2;
        }
        XFreeGC(d,sg);XFreePixmap(d,src);
    }
    unsigned long color(const char* name){
        XColor a,b;return XAllocNamedColor(d,DefaultColormap(d,DefaultScreen(d)),name,&a,&b)?a.pixel:WhitePixel(d,DefaultScreen(d));
    }
    void resize(int nw,int nh){
        w=std::max(1,nw);h=std::max(1,nh);
        if(pix)XFreePixmap(d,pix);pix=XCreatePixmap(d,win,w,h,depth);
    }
    int dim(int x)const{return x*scale/1024;}
    int sx(int x)const{return offsetX+dim(x);}
    int sy(int y)const{return offsetY+dim(y);}
    int rowHeight()const{return fontH+3;}
    int toolbarHeight()const{return std::max(32,fontH+12);}
    void layout(bool companion){
        int top=companion?0:rowHeight()+4;
        int footer=companion?0:rowHeight()+8;
        int replyRows=h>=350?3:2;
        int bottom=companion?std::max(1,h-toolbarHeight()):std::max(top+1,h-footer-replyRows*rowHeight()-8);
        // Preserve the original shape proportions and center the complete
        // face (eye housing + mouth) in the area above the chat.
        scale=std::max(1,std::min(w*1024/480,(bottom-top)*1024/184));
        offsetX=(w-dim(480))/2;
        offsetY=top+(bottom-top-dim(184))/2-dim(48);
    }
    void fg(unsigned long c){XSetForeground(d,gc,c);}
    void rect(int x,int y,int a,int b,unsigned long c){
        if(a<=0||b<=0)return;fg(c);XFillRectangle(d,pix,gc,sx(x),sy(y),std::max(1,dim(a)),std::max(1,dim(b)));
    }
    void rounded(int x,int y,int a,int b,int r,unsigned long c){
        r=std::min(r,std::min(a,b)/2); if(r<2){rect(x,y,a,b,c);return;}
        rect(x+r,y,a-2*r,b,c);rect(x,y+r,a,b-2*r,c);fg(c);
        for(int xx:{x,x+a-2*r})for(int yy:{y,y+b-2*r})
            XFillArc(d,pix,gc,sx(xx),sy(yy),std::max(1,dim(2*r)),std::max(1,dim(2*r)),0,360*64);
    }
    void line(int x,int y,int a,int b,unsigned long c,int thick=2){
        fg(c);XSetLineAttributes(d,gc,std::max(1,dim(thick)),LineSolid,CapButt,JoinMiter);
        XDrawLine(d,pix,gc,sx(x),sy(y),sx(a),sy(b));
    }
    void text(int x,int y,const std::string& s,unsigned long c){
        fg(c);
        if(!fontAtlas){XDrawString(d,pix,gc,x,y,s.c_str(),int(s.size()));return;}
        XSetBackground(d,gc,black);
        for(unsigned char ch:s){
            int idx=(ch>=32&&ch<=126)?ch-32:'?'-32;
            XCopyPlane(d,fontAtlas,pix,gc,idx*fontW,0,fontW,fontH,x,y-fontAscent,1);
            x+=fontW;
        }
    }
    void wrap(const std::string& s,int y,int maxlines){
        int cw=fontW;
        int cap=std::max(8,(w-20)/cw);
        size_t pos=0;
        for(int row=0;row<maxlines&&pos<s.size();row++){
            size_t end=std::min(s.size(),pos+size_t(cap));
            if(end<s.size()){size_t space=s.rfind(' ',end);if(space!=std::string::npos&&space>pos)end=space;}
            std::string part=s.substr(pos,end-pos);
            if(row==maxlines-1&&end<s.size()&&part.size()>3)part.replace(part.size()-3,3,"...");
            text(10,y+row*rowHeight(),part,green);
            pos=end;while(pos<s.size()&&s[pos]==' ')pos++;
        }
    }
    void keyRow(int y,int hh,const std::vector<std::pair<std::string,std::string>>& row){
        int n=int(row.size());
        for(int i=0;i<n;i++){
            int x=i*w/n,ww=(i+1)*w/n-x;
            if(ww<3||hh<3)continue;
            const auto& k=row[i];
            keys.push_back({x,y,ww,hh,k.second});
            fg(outline);XSetLineAttributes(d,gc,1,LineSolid,CapButt,JoinMiter);
            XDrawRectangle(d,pix,gc,x+1,y+1,ww-3,hh-3);
            text(x+std::max(2,(ww-int(k.first.size())*fontW)/2),
                 y+std::max(fontAscent,(hh-fontH)/2+fontAscent),k.first,green);
        }
    }
    std::string hit(int x,int y)const{
        for(const auto& k:keys)if(x>=k.x&&x<k.x+k.w&&y>=k.y&&y<k.y+k.h)return k.action;
        return "";
    }
    void keyboard(const Child& bot,const std::string& input,bool shift,bool symbols){
        int row=std::min(44,std::max(18,(h-2*rowHeight()-8)/5));
        int top=h-5*row;
        wrap(bot.reply,fontAscent+2,std::max(0,std::min(2,(top-rowHeight())/rowHeight())));
        int cap=std::max(4,(w-20)/fontW-3);
        std::string shown=input.size()>size_t(cap)?input.substr(input.size()-cap):input;
        text(8,top-5-(fontH-fontAscent),"> "+shown+"_",green);
        const char* letters[4]={"1234567890","qwertyuiop","asdfghjkl","zxcvbnm,.?"};
        const char* signs[4]={"!@#$%^&*()","-_=+[]{}<>",";:'\"/\\|~","0123456789"};
        for(int r=0;r<4;r++){
            std::vector<std::pair<std::string,std::string>> buttons;
            for(const char* p=(symbols?signs[r]:letters[r]);*p;p++){
                char ch=*p;if(shift&&!symbols&&ch>='a'&&ch<='z')ch-=32;
                std::string s(1,ch);buttons.push_back({s,"char:"+s});
            }
            keyRow(top+r*row,row,buttons);
        }
        keyRow(top+4*row,row,{{shift?"SHIFT":"Shift","shift"},{symbols?"ABC":"Sym","symbols"},{"Space","char: "},{"Del","delete"},{bot.active()?"Stop":"Send",bot.active()?"stop":"send"},{"Hide","hide"}});
    }
    void toggle(){
        Atom state=XInternAtom(d,"_NET_WM_STATE",False),full=XInternAtom(d,"_NET_WM_STATE_FULLSCREEN",False);
        XEvent e{};e.xclient.type=ClientMessage;e.xclient.window=win;e.xclient.message_type=state;
        e.xclient.format=32;e.xclient.data.l[0]=fullscreen?0:1;e.xclient.data.l[1]=full;e.xclient.data.l[3]=1;
        XSendEvent(d,DefaultRootWindow(d),False,SubstructureRedirectMask|SubstructureNotifyMask,&e);
        fullscreen=!fullscreen;
    }
    void draw(int64_t now,int64_t last,int64_t touched,const Child& bot,const std::string& input,bool companion,bool typing,bool shift,bool symbols,bool forcedSleep,int gesture,bool hingeAvailable){
        keys.clear();
        fg(black);XFillRectangle(d,pix,gc,0,0,w,h);
        if(companion&&typing){keyboard(bot,input,shift,symbols);XCopyArea(d,pix,win,gc,0,0,w,h,0,0);XFlush(d);return;}
        bool thinking=bot.active()&&!bot.received,talking=bot.active()&&bot.received;
        bool sleepy=!bot.active()&&(forcedSleep||now-last>45000),react=now-touched<850;
        layout(companion);
        int eyeY=108;
        int phase=int(now%5200),blink=(phase>5000)?std::max(8,std::abs(phase-5100)*94/100):94;
        int height=sleepy?23:react?(gesture==2?65:gesture==3?38:102):94;height=std::min(height,blink);
        int look=int((now/100)%80);look=(look<40?look:80-look)-20;
        Window root,child;int rx,ry,px,py;unsigned int mask;
        bool pointer=XQueryPointer(d,win,&root,&child,&rx,&ry,&px,&py,&mask);
        if(pointer&&(px!=pointerX||py!=pointerY)){pointerMoved=now;pointerX=px;pointerY=py;}
        int eye=0;
        for(int x:{113,367}){
            int tx=thinking?look*3/4:look/3,ty=0;
            if(!thinking&&!talking&&!sleepy&&pointer&&now-pointerMoved<12000){
                tx=clamp((px-sx(x))*1024/std::max(1,scale)/5,-20,20);
                ty=clamp((py-sy(eyeY))*1024/std::max(1,scale)/7,-9,9);
            }
            if(react&&gesture==2){tx=0;ty=-5;}
            if(sleepy){tx=0;ty=0;}
            auto ease=[](int a,int b){int delta=b-a;return a+(delta==0?0:(delta>0?1:-1)*std::max(1,std::abs(delta)/3));};
            pupilX[eye]=ease(pupilX[eye],tx);pupilY[eye]=ease(pupilY[eye],ty);
            int dx=pupilX[eye],dy=clamp(pupilY[eye],-height*12/100,height*12/100);
            eye++;
            // Outer shell stays put; the inner eye blinks inside it.
            rounded(x-62,eyeY-56,124,112,29,outline);
            rounded(x-60,eyeY-54,120,108,27,outer);
            rounded(x-53,eyeY-height/2,106,height,22,inner);
            int ph=std::max(4,height*76/100);
            rounded(x-29+dx,eyeY-ph/2+dy,58,ph,16,green);
            if(ph>40)rounded(x-18+dx,eyeY-ph/2+dy+10,10,17,5,highlight);
        }
        int mouth=213;
        if(talking||thinking){
            // Integer step waveform, no audio processing or floating-point trig.
            int prev=mouth;
            for(int i=0;i<14;i++){
                int k=(i+int(now/(talking?85:170)))%8;
                static const int wave[]={0,1,2,1,0,-1,-2,-1};
                int y=mouth+wave[k]*(talking?7:2);
                if(i==0)prev=mouth;
                line(170+i*10,prev,170+i*10,y,green,4);
                line(170+i*10,y,180+i*10,y,green,4);prev=y;
            }
            line(310,prev,310,mouth,green,4);
        }else if(sleepy||(react&&gesture==3)){line(177,mouth,303,mouth,green,4);}
        else if(react&&gesture==1){rounded(226,mouth-8,28,16,3,green);rounded(230,mouth-4,20,8,1,black);}
        else{
            // Square U-shaped smile rather than a rounded parenthesis.
            line(177,mouth-8,177,mouth+8,green,4);
            line(177,mouth+8,303,mouth+8,green,4);
            line(303,mouth+8,303,mouth-8,green,4);
        }
        if(!companion){
            int row=rowHeight(),rows=h>=350?3:2;
            int inputLine=h-row-8;
            text(10,fontAscent+3,"RELAY",muted);
            keys.push_back({0,0,6*fontW,rowHeight()+4,"mode"});
            std::string state=thinking?"THINKING":talking?"TALKING":sleepy?"SLEEPY":hingeAvailable?"ONLINE":"MANUAL";
            text(std::max(10,w-int(state.size())*fontW-10),fontAscent+3,state,muted);
            wrap(bot.reply,inputLine-rows*row-4+fontAscent,rows);
            fg(outline);XSetLineAttributes(d,gc,1,LineSolid,CapButt,JoinMiter);
            XDrawLine(d,pix,gc,8,inputLine,w-8,inputLine);
            int cap=std::max(8,(w-25)/fontW);
            std::string shown=input.size()>size_t(cap-3)?input.substr(input.size()-(cap-3)):input;
            text(10,h-6-(fontH-fontAscent),"> "+shown+(bot.active()?"":"_"),green);
        }
        if(companion){
            keyRow(h-toolbarHeight(),toolbarHeight(),{{"Type","type"},{forcedSleep?"Wake":"Sleep","sleep"},{"Chat","mode"},{"Exit","exit"}});
        }
        XCopyArea(d,pix,win,gc,0,0,w,h,0,0);XFlush(d);
    }
};
int main(int argc,char**argv){
    if(argc>1&&std::string(argv[1])=="--self-test"){std::puts("RELAY X11 face executable OK");return 0;}
    Display* d=XOpenDisplay(nullptr);
    if(!d){std::fprintf(stderr,"RELAY face needs the Brainux X11 desktop. Use sh chat.sh for terminal mode.\n");return 1;}
    {
        Face f(d); Child bot; Hinge hinge; std::string input;
        bool running=true,companion=false,typing=false,shift=false,symbols=false,forcedSleep=false;
        int seenTablet=-1,gesture=0,tapCount=0;
        int64_t pressAt=-1;int pressX=0,pressY=0;
        Atom close=XInternAtom(d,"WM_DELETE_WINDOW",False);XSetWMProtocols(d,f.win,&close,1);
        int64_t last=ms(),touched=-10000,next=0;
        auto send=[&](){if(!bot.active()&&!input.empty()){if(bot.start(input))input.clear();else bot.reply="Could not start model.";}};
        while(running){
            while(XPending(d)){
                XEvent e;XNextEvent(d,&e);
                if(e.type==ClientMessage&&Atom(e.xclient.data.l[0])==close)running=false;
                if(e.type==ConfigureNotify&&(e.xconfigure.width!=f.w||e.xconfigure.height!=f.h))f.resize(e.xconfigure.width,e.xconfigure.height);
                if(e.type==ButtonPress){
                    int64_t now=ms();last=now;
                    std::string action=f.hit(e.xbutton.x,e.xbutton.y);
                    pressAt=-1;
                    if(action=="type"){typing=true;forcedSleep=false;}
                    else if(action=="hide")typing=false;
                    else if(action=="shift")shift=!shift;
                    else if(action=="symbols")symbols=!symbols;
                    else if(action=="delete"){if(!input.empty())input.pop_back();}
                    else if(action=="send")send();
                    else if(action=="stop")bot.stop();
                    else if(action=="sleep")forcedSleep=!forcedSleep;
                    else if(action=="mode"){companion=!companion;typing=false;forcedSleep=false;}
                    else if(action=="exit")running=false;
                    else if(action.compare(0,5,"char:")==0){if(input.size()<140)input+=action.substr(5);shift=false;}
                    else if(!typing){forcedSleep=false;pressAt=now;pressX=e.xbutton.x;pressY=e.xbutton.y;}
                    next=0;
                }
                if(e.type==ButtonRelease&&pressAt>=0){
                    int64_t now=ms();
                    if(now-touched<900)tapCount++;else tapCount=1;
                    bool pat=now-pressAt>=350 || (pressY<f.h/3&&std::abs(e.xbutton.x-pressX)>15);
                    gesture=pat?2:tapCount>=3?3:1;touched=last=now;pressAt=-1;next=0;
                }
                if(e.type==KeyPress){
                    last=ms();char b[32];KeySym key;int n=XLookupString(&e.xkey,b,sizeof b,&key,nullptr);
                    if(key==XK_Escape){if(typing)typing=false;else if(companion)companion=false;else if(bot.active())bot.stop();else running=false;}
                    else if(key==XK_F11)f.toggle();
                    else if(key==XK_F2){companion=!companion;typing=false;}
                    else if(key==XK_Return||key==XK_KP_Enter){
                        send();
                    }else if(key==XK_BackSpace){if(!input.empty())input.pop_back();}
                    else for(int i=0;i<n&&input.size()<140;i++)if((unsigned char)b[i]>=32&&(unsigned char)b[i]<127){input+=b[i];if(!typing)companion=false;}
                }
            }
            bot.update();int64_t now=ms();
            hinge.update(now);
            if(hinge.tablet>=0&&hinge.tablet!=seenTablet){seenTablet=hinge.tablet;companion=seenTablet==1;typing=false;forcedSleep=false;last=now;next=0;}
            if(hinge.lid!=1&&now>=next){f.draw(now,last,touched,bot,input,companion,typing,shift,symbols,forcedSleep,gesture,hinge.fd>=0);next=now+(bot.active()?100:typing?150:67);}
            else if(hinge.lid==1)next=now+250;
            struct pollfd p[2]={{ConnectionNumber(d),POLLIN,0},{bot.fd,POLLIN,0}};
            poll(p,bot.active()?2:1,clamp(int(next-ms()),0,100));
        }
    }
    XCloseDisplay(d);return 0;
}
