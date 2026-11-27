// RELAY native X11 face. No image assets or graphics toolkit required.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
using Clock=std::chrono::steady_clock;
static long ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
static int clamp(int n,int lo,int hi){return std::max(lo,std::min(hi,n));}
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
    unsigned long black,outer,outline,inner,green,highlight,muted;
    Face(Display* display):d(display){
        int screen=DefaultScreen(d);depth=DefaultDepth(d,screen);
        win=XCreateSimpleWindow(d,RootWindow(d,screen),0,0,w,h,0,0,0);
        XStoreName(d,win,"RELAY");gc=XCreateGC(d,win,0,nullptr);
        font=XLoadQueryFont(d,"fixed");if(font)XSetFont(d,gc,font->fid);
        black=color("#030604");outer=color("#0a2115");outline=color("#216d40");
        inner=color("#164e2e");green=color("#57ee91");highlight=color("#c0ffda");muted=color("#82b997");
        XSelectInput(d,win,ExposureMask|KeyPressMask|ButtonPressMask|StructureNotifyMask);
        XMapWindow(d,win); resize(w,h);
    }
    ~Face(){if(pix)XFreePixmap(d,pix);if(font)XFreeFont(d,font);XFreeGC(d,gc);XDestroyWindow(d,win);}
    unsigned long color(const char* name){
        XColor a,b;return XAllocNamedColor(d,DefaultColormap(d,DefaultScreen(d)),name,&a,&b)?a.pixel:WhitePixel(d,DefaultScreen(d));
    }
    void resize(int nw,int nh){
        w=std::max(1,nw);h=std::max(1,nh);
        if(pix)XFreePixmap(d,pix);pix=XCreatePixmap(d,win,w,h,depth);
    }
    int sx(int x)const{return x*w/480;} int sy(int y)const{return y*h/272;}
    void fg(unsigned long c){XSetForeground(d,gc,c);}
    void rect(int x,int y,int a,int b,unsigned long c){
        if(a<=0||b<=0)return;fg(c);XFillRectangle(d,pix,gc,sx(x),sy(y),std::max(1,sx(a)),std::max(1,sy(b)));
    }
    void rounded(int x,int y,int a,int b,int r,unsigned long c){
        r=std::min(r,std::min(a,b)/2); if(r<2){rect(x,y,a,b,c);return;}
        rect(x+r,y,a-2*r,b,c);rect(x,y+r,a,b-2*r,c);fg(c);
        for(int xx:{x,x+a-2*r})for(int yy:{y,y+b-2*r})
            XFillArc(d,pix,gc,sx(xx),sy(yy),std::max(1,sx(2*r)),std::max(1,sy(2*r)),0,360*64);
    }
    void line(int x,int y,int a,int b,unsigned long c,int thick=2){
        fg(c);XSetLineAttributes(d,gc,std::max(1,sx(thick)),LineSolid,CapButt,JoinMiter);
        XDrawLine(d,pix,gc,sx(x),sy(y),sx(a),sy(b));
    }
    void text(int x,int y,const std::string& s,unsigned long c){
        fg(c);XDrawString(d,pix,gc,x,y,s.c_str(),int(s.size()));
    }
    void wrap(const std::string& s,int y,int maxlines){
        int cw=font?std::max(1,int(font->max_bounds.width)):6;
        int cap=std::max(8,(w-20)/cw);
        size_t pos=0;
        for(int row=0;row<maxlines&&pos<s.size();row++){
            size_t end=std::min(s.size(),pos+size_t(cap));
            if(end<s.size()){size_t space=s.rfind(' ',end);if(space!=std::string::npos&&space>pos)end=space;}
            std::string part=s.substr(pos,end-pos);
            if(row==maxlines-1&&end<s.size()&&part.size()>3)part.replace(part.size()-3,3,"...");
            text(10,y+row*13,part,green);
            pos=end;while(pos<s.size()&&s[pos]==' ')pos++;
        }
    }
    void toggle(){
        Atom state=XInternAtom(d,"_NET_WM_STATE",False),full=XInternAtom(d,"_NET_WM_STATE_FULLSCREEN",False);
        XEvent e{};e.xclient.type=ClientMessage;e.xclient.window=win;e.xclient.message_type=state;
        e.xclient.format=32;e.xclient.data.l[0]=fullscreen?0:1;e.xclient.data.l[1]=full;e.xclient.data.l[3]=1;
        XSendEvent(d,DefaultRootWindow(d),False,SubstructureRedirectMask|SubstructureNotifyMask,&e);
        fullscreen=!fullscreen;
    }
    void draw(long now,long last,long touched,const Child& bot,const std::string& input,bool companion){
        fg(black);XFillRectangle(d,pix,gc,0,0,w,h);
        bool thinking=bot.active()&&!bot.received,talking=bot.active()&&bot.received;
        bool sleepy=!bot.active()&&now-last>45000,react=now-touched<850;
        int eyeY=companion?108:84;
        int phase=int(now%5200),blink=(phase>5000)?std::max(8,std::abs(phase-5100)*94/100):94;
        int height=sleepy?23:react?102:94;height=std::min(height,blink);
        int look=int((now/100)%80);look=(look<40?look:80-look)-20;
        int dx=thinking?look*3/4:react?12:look/3;
        for(int x:{113,367}){
            // Outer shell stays put; the inner eye blinks inside it.
            rounded(x-62,eyeY-56,124,112,29,outline);
            rounded(x-60,eyeY-54,120,108,27,outer);
            rounded(x-53,eyeY-height/2,106,height,22,inner);
            int ph=std::max(4,height*76/100);
            rounded(x-29+dx,eyeY-ph/2,58,ph,16,green);
            if(ph>40)rounded(x-18+dx,eyeY-ph/2+10,10,17,5,highlight);
        }
        int mouth=companion?213:154;
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
        }else if(sleepy){line(177,mouth,303,mouth,green,4);}
        else{
            // Square U-shaped smile rather than a rounded parenthesis.
            line(177,mouth-8,177,mouth+8,green,4);
            line(177,mouth+8,303,mouth+8,green,4);
            line(303,mouth+8,303,mouth-8,green,4);
        }
        if(!companion){
            text(10,13,"RELAY",muted);
            std::string state=thinking?"THINKING":talking?"TALKING":sleepy?"SLEEPY":"ONLINE";
            text(std::max(10,w-int(state.size())*7-10),13,state,muted);
            wrap(bot.reply,sy(180),std::max(1,(h-sy(180)-34)/13));
            fg(outline);XDrawLine(d,pix,gc,8,h-28,w-8,h-28);
            int cap=std::max(8,(w-25)/6);
            std::string shown=input.size()>size_t(cap-3)?input.substr(input.size()-(cap-3)):input;
            text(10,h-12,"> "+shown+(bot.active()?"":"_"),green);
        }
        XCopyArea(d,pix,win,gc,0,0,w,h,0,0);XFlush(d);
    }
};
int main(int argc,char**argv){
    if(argc>1&&std::string(argv[1])=="--self-test"){std::puts("RELAY X11 face executable OK");return 0;}
    Display* d=XOpenDisplay(nullptr);
    if(!d){std::fprintf(stderr,"RELAY face needs the Brainux X11 desktop. Use sh chat.sh for terminal mode.\n");return 1;}
    {
        Face f(d); Child bot; std::string input; bool running=true,companion=false;
        Atom close=XInternAtom(d,"WM_DELETE_WINDOW",False);XSetWMProtocols(d,f.win,&close,1);
        long last=ms(),touched=-10000,next=0;
        while(running){
            while(XPending(d)){
                XEvent e;XNextEvent(d,&e);
                if(e.type==ClientMessage&&Atom(e.xclient.data.l[0])==close)running=false;
                if(e.type==ConfigureNotify&&(e.xconfigure.width!=f.w||e.xconfigure.height!=f.h))f.resize(e.xconfigure.width,e.xconfigure.height);
                if(e.type==ButtonPress){long now=ms();if(companion&&now-touched<400)companion=false;touched=last=now;}
                if(e.type==KeyPress){
                    last=ms();char b[32];KeySym key;int n=XLookupString(&e.xkey,b,sizeof b,&key,nullptr);
                    if(key==XK_Escape){if(companion)companion=false;else if(bot.active())bot.stop();else running=false;}
                    else if(key==XK_F11)f.toggle();
                    else if(key==XK_F2)companion=!companion;
                    else if(key==XK_Return||key==XK_KP_Enter){
                        if(!bot.active()&&!input.empty()){if(bot.start(input))input.clear();else bot.reply="Could not start model.";}
                    }else if(key==XK_BackSpace){if(!input.empty())input.pop_back();}
                    else for(int i=0;i<n&&input.size()<140;i++)if((unsigned char)b[i]>=32&&(unsigned char)b[i]<127){input+=b[i];companion=false;}
                }
            }
            bot.update();long now=ms();
            if(now>=next){f.draw(now,last,touched,bot,input,companion);next=now+(bot.active()?100:67);}
            struct pollfd p[2]={{ConnectionNumber(d),POLLIN,0},{bot.fd,POLLIN,0}};
            poll(p,bot.active()?2:1,clamp(int(next-ms()),0,100));
        }
    }
    XCloseDisplay(d);return 0;
}
