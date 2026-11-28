// RELAY native X11 face. No image assets or graphics toolkit required.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <algorithm>
#include <initializer_list>
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
// Visual-only mood cues. These never select or modify the model's text.
enum Mood {Neutral,Happy,Annoyed,Concerned,Curious};
static std::string words(const std::string& text){
    std::string out=" ";
    for(unsigned char c:text){
        if(c>='A'&&c<='Z')c+=32;
        if((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='\'')out+=char(c);
        else if(out.back()!=' ')out+=' ';
    }
    if(out.back()!=' ')out+=' ';
    return out;
}
static bool has(const std::string& s,std::initializer_list<const char*> phrases){
    for(const char* p:phrases)if(s.find(std::string(" ")+p+" ")!=std::string::npos)return true;
    return false;
}
static Mood userMood(const std::string& text){
    auto s=words(text);
    if(has(s,{"i'm sad","im sad","i am sad","i feel sad","i feel lonely","i'm upset","im upset","i am upset","i had a bad day","i feel terrible","i'm having a bad day"}))return Concerned;
    if(has(s,{"you look stupid","you are stupid","you're stupid","youre stupid","you look ugly","you're ugly","you are ugly","you suck","you're annoying","you are annoying","shut up","i hate you","you're not cute","you are not cute","you look dumb","you idiot","you are useless","you're useless"}))return Annoyed;
    if(has(s,{"you look cute","you're cute","youre cute","you are cute","so cute","you're adorable","you are adorable","you look adorable","you look nice","you look cool","you're cool","you are cool","good job","well done","i like you","i love you","you're my favorite","you are my favorite","thank you","thanks relay","thanks","i fixed you","you look good","you're awesome","you are awesome","you're amazing","you are amazing"})&&!has(s,{"not cute","not adorable","not cool","not good","don't like you","dont like you"}))return Happy;
    if(has(s,{"what if","imagine if","did you know","have you ever"}))return Curious;
    return Neutral;
}
static Mood replyMood(const std::string& text){
    auto s=words(text);
    if(has(s,{"that's rude","thats rude","how rude","not funny","leave me alone","i'm annoyed"}))return Annoyed;
    if(has(s,{"i'm sorry to hear","sorry to hear","that sounds difficult","are you okay","what's wrong"}))return Concerned;
    if(has(s,{"thank you","thanks","that's great","thats great","i'm happy","im happy","that's awesome","i appreciate it"}))return Happy;
    return Neutral;
}
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
    bool start(const std::string& message,int temperature){
        if(active())return false;
        int p[2]; if(pipe(p)<0)return false;
        std::string prompt="User: "+message+"\nBot:";
        std::string seed=std::to_string(std::time(nullptr));
        char temp[16];std::snprintf(temp,sizeof temp,"%d.%02d",temperature/100,temperature%100);
        pid=fork();
        if(pid==0){
            close(p[0]); dup2(p[1],STDOUT_FILENO); close(p[1]);
            int log=open(".relay-debug.log",O_WRONLY|O_CREAT|O_TRUNC,0600);
            if(log>=0){dup2(log,STDERR_FILENO);close(log);}
            execl("./tinytalk","tinytalk","model_neo_q4.bin","tok_neo.bin",prompt.c_str(),
                  "--max","20","--temp",temp,"--top-p","0.9","--kv","72","--seed",seed.c_str(),(char*)nullptr);
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
struct Preferences {
    int style=0,palette=1,effect=0,sleep=45,tracking=1,autoHinge=1,temp=65;
    void load(){
        FILE* f=std::fopen("relay.conf","r");if(!f)return;
        char key[32];int value;
        while(std::fscanf(f,"%31s %d",key,&value)==2){
            std::string k=key;
            if(k=="style")style=clamp(value,0,4);
            else if(k=="palette")palette=clamp(value,0,5);
            else if(k=="effect")effect=clamp(value,0,3);
            else if(k=="sleep")sleep=clamp(value,0,120);
            else if(k=="tracking")tracking=value!=0;
            else if(k=="autoHinge")autoHinge=value!=0;
            else if(k=="temp")temp=clamp(value,10,120);
        }
        std::fclose(f);
    }
    bool save()const{
        FILE* f=std::fopen("relay.conf.tmp","w");if(!f)return false;
        int rc=std::fprintf(f,"style %d\npalette %d\neffect %d\nsleep %d\ntracking %d\nautoHinge %d\ntemp %d\n",style,palette,effect,sleep,tracking,autoHinge,temp);
        int closed=std::fclose(f);
        return rc>=0&&closed==0&&std::rename("relay.conf.tmp","relay.conf")==0;
    }
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
    unsigned long black,outer,outline,inner,green,highlight,muted,sweepColor,scanColor;
    Preferences prefs; bool saved=true; int faceTop=0,faceBottom=0;
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
        prefs.load();applyPalette();
        XSelectInput(d,win,ExposureMask|KeyPressMask|ButtonPressMask|ButtonReleaseMask|ButtonMotionMask|StructureNotifyMask);
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
    void applyPalette(){
        // Allocate once per palette; release prior allocations on pseudocolor too.
        static const char* p[6][9]={
          {"#292008","#342909","#786025","#453611","#e8b94d","#f0d48b","#c7a25d","#59451b","#382b0d"},
          {"#08140a","#102013","#355039","#172d19","#81c96b","#a6e18e","#88ad7e","#30442d","#122016"},
          {"#a7ae91","#a0a88b","#727d66","#939e82","#29372a","#41513d","#3f4c36","#858f73","#9aa486"},
          {"#08191e","#10292f","#31545e","#193a42","#76c3ca","#a0d5d9","#83aeb3","#28434c","#12272e"},
          {"#200b09","#2c110d","#6b332a","#451913","#dc6850","#e9a18a","#c68976","#50241e","#2e130f"},
          {"#171919","#222625","#525957","#333a38","#c7d0c8","#e4e8e3","#aab4ac","#444947","#262a28"}
        };
        if(paletteReady){unsigned long old[]={black,outer,outline,inner,green,highlight,muted,sweepColor,scanColor};XFreeColors(d,DefaultColormap(d,DefaultScreen(d)),old,9,0);}
        auto a=p[prefs.palette];
        black=color(a[0]);outer=color(a[1]);outline=color(a[2]);inner=color(a[3]);green=color(a[4]);highlight=color(a[5]);muted=color(a[6]);sweepColor=color(a[7]);scanColor=color(a[8]);paletteReady=true;
    }
    bool paletteReady=false;
    void resize(int nw,int nh){
        w=std::max(1,nw);h=std::max(1,nh);
        if(pix)XFreePixmap(d,pix);pix=XCreatePixmap(d,win,w,h,depth);
    }
    int dim(int x)const{return x*scale/1024;}
    int sx(int x)const{return offsetX+dim(x);}
    int sy(int y)const{return offsetY+dim(y);}
    int rowHeight()const{return fontH+3;}
    int toolbarHeight()const{return std::max(32,fontH+12);}
    int replyRows(const std::string& s,bool companion)const{
        int cap=std::max(8,(w-20)/fontW),limit=companion?(h>=350?4:3):(h>=350?3:2);
        int rows=0;size_t pos=0;
        while(pos<s.size()&&rows<limit){
            size_t end=std::min(s.size(),pos+size_t(cap));
            if(end<s.size()){size_t space=s.rfind(' ',end);if(space!=std::string::npos&&space>pos)end=space;}
            pos=end;while(pos<s.size()&&s[pos]==' ')pos++;
            rows++;
        }
        return rows;
    }
    int inputLine()const{return h-rowHeight()-8;}
    int replyTop(bool companion,int rows)const{
        return (companion?h-toolbarHeight():inputLine())-rows*rowHeight()-(rows?8:0);
    }
    void layout(bool companion,int rows,bool settings){
        int top=companion?4:rowHeight()+4;
        int bottom=replyTop(companion,rows)-4;
        if(settings){top=0;bottom=std::max(1,h-6*toolbarHeight());}
        bottom=std::max(top+1,bottom);
        faceTop=top;faceBottom=bottom;
        // Center around the LCD midpoint, shifting only for long replies.
        int target=settings?(top+bottom)/2:h/2;
        int desiredHalf=std::max(1,std::min(target-top+12,bottom-target+12));
        int fit=std::min(bottom-top,desiredHalf*2);
        if(fit<70)fit=bottom-top;
        scale=std::max(1,std::min(w*1024/520,fit*1024/184));
        offsetX=(w-dim(480))/2;
        int half=dim(184)/2;
        int center=clamp(target,top+half,std::max(top+half,bottom-half));
        offsetY=center-half-dim(48);
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
    void retroEye(int x,int y,int height,int dx,int dy){
        int ph=std::max(4,height*76/100);
        if(prefs.style==0 || prefs.style==3 || prefs.style==1){
            rect(x-62,y-56,124,112,outline);rect(x-59,y-53,118,106,outer);
        }
        if(prefs.style==0){
            rect(x-50,y-height/2,100,height,inner);
            rect(x-28+dx,y-ph/2+dy,56,ph,green);
        }else if(prefs.style==1){
            int n=std::max(1,(ph+13)/14);
            for(int i=0;i<n;i++)rect(x-29+dx,y-n*7+i*14+dy,58,10,green);
        }else if(prefs.style==2){
            // Small square matrix; batch lit pixels in one X request per eye.
            std::vector<XRectangle> dots;int rows=clamp((height+12)/14,1,7);
            for(int r=0;r<rows;r++)for(int c=0;c<7;c++){
                if(rows>4&&(r==0||r==rows-1)&&(c==0||c==6))continue;
                dots.push_back(XRectangle{short(sx(x-46+c*14+dx/2)),short(sy(y-rows*7+r*14+dy/2)),static_cast<unsigned short>(std::max(1,dim(10))),static_cast<unsigned short>(std::max(1,dim(10)))});
            }
            fg(green);if(!dots.empty())XFillRectangles(d,pix,gc,dots.data(),int(dots.size()));
        }else if(prefs.style==3){
            rect(x-45,y-height/4,90,std::max(4,height/2),inner);
            rect(x-29+dx,y-std::max(2,ph/4)+dy,58,std::max(4,ph/2),green);
            rect(x-29+dx,y+std::max(3,ph/4)+dy,58,3,muted);
        }else{
            rect(x-34+dx,y-ph/2+dy,68,ph,green);
        }
    }
    void mouthLine(int x,int y,int xx,int yy){
        if(prefs.style==2){
            int n=std::max(1,std::max(std::abs(xx-x),std::abs(yy-y))/9);
            for(int i=0;i<=n;i++)rect(x+(xx-x)*i/n-2,y+(yy-y)*i/n-2,5,5,green);
        }else if(prefs.style==1){
            int n=std::max(1,std::max(std::abs(xx-x),std::abs(yy-y))/10);
            for(int i=0;i<n;i++){
                int a=x+(xx-x)*i/n,b=y+(yy-y)*i/n;
                rect(a-2,b-2,xx==x?5:7,yy==y?5:7,green);
            }
        }else line(x,y,xx,yy,green,prefs.style==3?2:4);
    }
    void crt(int64_t now){
        if(!prefs.effect||faceBottom<=faceTop)return;
        // Low-contrast raster marks confined to the face, not readable text.
        // No screen-wide flashes, alpha blending, or shader passes.
        if(prefs.effect&1){
            std::vector<XSegment> lines;
            for(int y=faceTop+4;y<faceBottom;y+=6)lines.push_back(XSegment{0,short(y),short(w-1),short(y)});
            fg(scanColor);XSetLineAttributes(d,gc,1,LineSolid,CapButt,JoinMiter);
            if(!lines.empty())XDrawSegments(d,pix,gc,lines.data(),int(lines.size()));
        }
        if(prefs.effect&2){
            int phase=int(now%8000);
            if(phase<2200){
                int y=faceTop+(faceBottom-faceTop)*phase/2200;
                fg(sweepColor);XFillRectangle(d,pix,gc,0,y,w,1);
            }
        }
    }
    void settingsRows(bool behavior){
        static const char* styles[]={"LCD Block","Segment","Dot Matrix","Terminal","Minimal"};
        static const char* palettes[]={"Amber","Green","Gray LCD","Blue VFD","Red LED","Mono CRT"};
        static const char* effects[]={"Off","Scanlines","Sweep","Both"};
        int hh=toolbarHeight(), y=h-6*hh;
        auto row=[&](const std::string& name,const std::string& value,const std::string& action){
            // Wide touch targets and one short label per half of the screen.
            keyRow(y,hh,{{name,""},{value,action}});y+=hh;
        };
        if(!behavior){
            row("Face style",styles[prefs.style],"style");
            row("Palette",palettes[prefs.palette],"palette");
            row("CRT effect",effects[prefs.effect],"effect");
            row("Settings saved",saved?"Yes":"Write error","save");
            keyRow(y,hh,{{"Behavior","behavior"},{"Defaults","defaults"}});y+=hh;
        }else{
            row("Eye tracking",prefs.tracking?"On":"Off","tracking");
            row("Auto fold",prefs.autoHinge?"On":"Off","autohinge");
            row("Sleep",prefs.sleep?std::to_string(prefs.sleep)+" sec":"Never","sleeptime");
            row("Randomness",std::to_string(prefs.temp)+"%","temperature");
            keyRow(y,hh,{{"Appearance","appearance"},{"Defaults","defaults"}});y+=hh;
        }
        keyRow(y,hh,{{"Back","settings"}});
    }
    void toggle(){
        Atom state=XInternAtom(d,"_NET_WM_STATE",False),full=XInternAtom(d,"_NET_WM_STATE_FULLSCREEN",False);
        XEvent e{};e.xclient.type=ClientMessage;e.xclient.window=win;e.xclient.message_type=state;
        e.xclient.format=32;e.xclient.data.l[0]=fullscreen?0:1;e.xclient.data.l[1]=full;e.xclient.data.l[3]=1;
        XSendEvent(d,DefaultRootWindow(d),False,SubstructureRedirectMask|SubstructureNotifyMask,&e);
        fullscreen=!fullscreen;
    }
    void draw(int64_t now,int64_t last,int64_t touched,const Child& bot,const std::string& input,bool companion,bool typing,bool shift,bool symbols,bool forcedSleep,int gesture,bool hingeAvailable,bool settings,bool behavior,Mood mood){
        keys.clear();
        fg(black);XFillRectangle(d,pix,gc,0,0,w,h);
        if(!settings&&companion&&typing){keyboard(bot,input,shift,symbols);XCopyArea(d,pix,win,gc,0,0,w,h,0,0);XFlush(d);return;}
        bool thinking=bot.active()&&!bot.received,talking=bot.active()&&bot.received;
        bool sleepy=!settings&&!bot.active()&&(forcedSleep||(prefs.sleep>0&&now-last>int64_t(prefs.sleep)*1000)),react=!settings&&now-touched<850;
        std::string shownReply=bot.active()&&bot.reply.empty()?"Thinking...":bot.reply;
        int rows=replyRows(shownReply,companion);
        layout(companion,rows,settings);
        int eyeY=108;
        int phase=int(now%5200),blink=(phase>5000)?std::max(8,std::abs(phase-5100)*94/100):94;
        Mood expression=settings?Neutral:mood;
        if(react&&gesture==2)expression=Happy;
        if(react&&gesture==3)expression=Annoyed;
        int height=sleepy?23:(react&&gesture==1)?102:expression==Happy?61:expression==Annoyed?34:expression==Concerned?55:94;
        int look=int((now/100)%80);look=(look<40?look:80-look)-20;
        Window root,child;int rx,ry,px,py;unsigned int mask;
        bool pointer=XQueryPointer(d,win,&root,&child,&rx,&ry,&px,&py,&mask);
        if(pointer&&(px!=pointerX||py!=pointerY)){pointerMoved=now;pointerX=px;pointerY=py;}
        int eye=0;
        for(int x:{113,367}){
            int tx=thinking?look*3/4:look/3,ty=0;
            if(prefs.tracking&&!thinking&&!talking&&!sleepy&&pointer&&now-pointerMoved<12000){
                tx=clamp((px-sx(x))*1024/std::max(1,scale)/5,-20,20);
                ty=clamp((py-sy(eyeY))*1024/std::max(1,scale)/7,-9,9);
            }
            if(expression==Happy){tx=0;ty=-5;}
            if(expression==Annoyed){tx=(eye==0?7:-7);ty=0;}
            if(expression==Concerned){tx=0;ty=5;}
            if(expression==Curious){tx=8;ty=-3;}
            if(sleepy){tx=0;ty=0;}
            auto ease=[](int a,int b){int delta=b-a;return a+(delta==0?0:(delta>0?1:-1)*std::max(1,std::abs(delta)/3));};
            pupilX[eye]=ease(pupilX[eye],tx);pupilY[eye]=ease(pupilY[eye],ty);
            int eh=expression==Curious&&eye==1&&!sleepy?height*2/3:height;
            eh=std::min(eh,blink);
            int dx=pupilX[eye],dy=clamp(pupilY[eye],-eh*12/100,eh*12/100);
            eye++;
            retroEye(x,eyeY,eh,dx,dy);
        }
        int mouth=213;
        if(talking||thinking){
            // Integer step waveform, no audio processing or floating-point trig.
            int prev=mouth;
            for(int i=0;i<14;i++){
                int k=(i+int(now/(talking?85:170)))%8;
                static const int wave[]={0,1,2,1,0,-1,-2,-1};
                int amplitude=expression==Annoyed?3:expression==Happy?7:talking?5:2;
                int y=mouth+wave[k]*amplitude;
                if(i==0)prev=mouth;
                mouthLine(170+i*10,prev,170+i*10,y);
                mouthLine(170+i*10,y,180+i*10,y);prev=y;
            }
            mouthLine(310,prev,310,mouth);
        }else if(sleepy||expression==Annoyed){mouthLine(177,mouth,303,mouth);}
        else if(react&&gesture==1){rect(226,mouth-8,28,16,green);rect(230,mouth-4,20,8,black);}
        else if(expression==Concerned){
            mouthLine(195,mouth+7,195,mouth-4);mouthLine(195,mouth-4,285,mouth-4);mouthLine(285,mouth-4,285,mouth+7);
        }else if(expression==Curious){mouthLine(220,mouth,260,mouth);}
        else if(expression==Neutral){mouthLine(199,mouth,281,mouth);}
        else{
            // Square U-shaped smile rather than a rounded parenthesis.
            mouthLine(177,mouth-8,177,mouth+8);
            mouthLine(177,mouth+8,303,mouth+8);
            mouthLine(303,mouth+8,303,mouth-8);
        }
        crt(now);
        if(settings){settingsRows(behavior);XCopyArea(d,pix,win,gc,0,0,w,h,0,0);XFlush(d);return;}
        if(!companion){
            int inputLine=this->inputLine();
            text(10,fontAscent+3,"RELAY",muted);
            keys.push_back({0,0,6*fontW,rowHeight()+4,"mode"});
            text(w/2-4*fontW,fontAscent+3,"Settings",green);
            keys.push_back({w/2-4*fontW,0,8*fontW,rowHeight()+4,"settings"});
            std::string state=thinking?"THINKING":talking?"TALKING":sleepy?"SLEEPY":hingeAvailable?"ONLINE":"MANUAL";
            text(std::max(10,w-int(state.size())*fontW-10),fontAscent+3,state,muted);
            if(rows)wrap(shownReply,replyTop(false,rows)+4+fontAscent,rows);
            fg(outline);XSetLineAttributes(d,gc,1,LineSolid,CapButt,JoinMiter);
            XDrawLine(d,pix,gc,8,inputLine,w-8,inputLine);
            int cap=std::max(8,(w-25)/fontW);
            std::string shown=input.size()>size_t(cap-3)?input.substr(input.size()-(cap-3)):input;
            text(10,h-6-(fontH-fontAscent),"> "+shown+(bot.active()?"":"_"),green);
        }
        if(companion){
            if(rows){
                int top=replyTop(true,rows);
                fg(outline);XSetLineAttributes(d,gc,1,LineSolid,CapButt,JoinMiter);
                XDrawLine(d,pix,gc,8,top,w-8,top);
                wrap(shownReply,top+4+fontAscent,rows);
            }
            keyRow(h-toolbarHeight(),toolbarHeight(),{{"Type","type"},{bot.active()?"Stop":forcedSleep?"Wake":"Sleep",bot.active()?"stop":"sleep"},{"Chat","mode"},{"Setup","settings"},{"Exit","exit"}});
        }
        XCopyArea(d,pix,win,gc,0,0,w,h,0,0);XFlush(d);
    }
};
int main(int argc,char**argv){
    if(argc>1&&std::string(argv[1])=="--self-test"){
        if(userMood("You look cute!")!=Happy||userMood("you look stupid")!=Annoyed||userMood("what do you look like?")!=Neutral||userMood("I am sad")!=Concerned||userMood("you are not cute")!=Annoyed||replyMood("Thank you!")!=Happy)return 2;
        std::puts("RELAY X11 face and mood cue tests OK");return 0;
    }
    Display* d=XOpenDisplay(nullptr);
    if(!d){std::fprintf(stderr,"RELAY face needs the Brainux X11 desktop. Use sh chat.sh for terminal mode.\n");return 1;}
    {
        Face f(d); Child bot; Hinge hinge; std::string input;
        bool running=true,companion=false,typing=false,shift=false,symbols=false,forcedSleep=false,settings=false,behavior=false;
        int seenTablet=-1,gesture=0,tapCount=0;
        int64_t pressAt=-1;int pressX=0,pressY=0;
        Atom close=XInternAtom(d,"WM_DELETE_WINDOW",False);XSetWMProtocols(d,f.win,&close,1);
        int64_t last=ms(),touched=-10000,next=0,moodUntil=0;
        Mood mood=Neutral, promptMood=Neutral;
        auto send=[&](){
            if(!bot.active()&&input.find_first_not_of(" \t\r\n")!=std::string::npos){
                if(bot.start(input,f.prefs.temp)){promptMood=mood=userMood(input);moodUntil=0;input.clear();typing=false;shift=false;symbols=false;forcedSleep=false;}
                else bot.reply="Could not start model.";
                last=ms();next=0;
            }
        };
        while(running){
            while(XPending(d)){
                XEvent e;XNextEvent(d,&e);
                if(e.type==ClientMessage&&Atom(e.xclient.data.l[0])==close)running=false;
                if(e.type==ConfigureNotify&&(e.xconfigure.width!=f.w||e.xconfigure.height!=f.h))f.resize(e.xconfigure.width,e.xconfigure.height);
                if(e.type==ButtonPress){
                    int64_t now=ms();last=now;
                    std::string action=f.hit(e.xbutton.x,e.xbutton.y);
                    pressAt=-1;
                    if(action=="settings"){settings=!settings;behavior=false;typing=false;}
                    else if(settings){
                        bool changed=true;
                        if(action=="style")f.prefs.style=(f.prefs.style+1)%5;
                        else if(action=="palette"){f.prefs.palette=(f.prefs.palette+1)%6;f.applyPalette();}
                        else if(action=="effect")f.prefs.effect=(f.prefs.effect+1)%4;
                        else if(action=="tracking")f.prefs.tracking=!f.prefs.tracking;
                        else if(action=="autohinge"){f.prefs.autoHinge=!f.prefs.autoHinge;seenTablet=-1;}
                        else if(action=="sleeptime"){int& t=f.prefs.sleep;t=t==0?15:t==15?45:t==45?120:0;}
                        else if(action=="temperature"){int& t=f.prefs.temp;t=t<40?40:t<65?65:t<85?85:t<100?100:20;}
                        else if(action=="defaults"){f.prefs=Preferences{};f.applyPalette();seenTablet=-1;}
                        else if(action=="save"){}
                        else if(action=="behavior"){behavior=true;changed=false;}
                        else if(action=="appearance"){behavior=false;changed=false;}
                        else changed=false;
                        if(changed)f.saved=f.prefs.save();
                    }
                    else if(action=="type"){typing=true;forcedSleep=false;}
                    else if(action=="hide")typing=false;
                    else if(action=="shift")shift=!shift;
                    else if(action=="symbols")symbols=!symbols;
                    else if(action=="delete"){if(!input.empty())input.pop_back();}
                    else if(action=="send")send();
                    else if(action=="stop"){bot.stop();mood=promptMood=Neutral;moodUntil=0;if(bot.reply.empty())bot.reply="Stopped."; }
                    else if(action=="sleep")forcedSleep=!forcedSleep;
                    else if(action=="mode"){companion=!companion;typing=false;forcedSleep=false;}
                    else if(action=="exit")running=false;
                    else if(action.compare(0,5,"char:")==0){if(input.size()<140)input+=action.substr(5);shift=false;}
                    else if(!typing){forcedSleep=false;pressAt=now;pressX=e.xbutton.x;pressY=e.xbutton.y;}
                    next=0;
                }
                if(e.type==MotionNotify&&pressAt>=0){
                    int64_t now=ms();last=now;
                    if(std::abs(e.xmotion.x-pressX)+std::abs(e.xmotion.y-pressY)>12){gesture=2;touched=now;next=0;}
                }
                if(e.type==ButtonRelease&&pressAt>=0){
                    int64_t now=ms();
                    if(now-touched<900)tapCount++;else tapCount=1;
                    bool pat=now-pressAt>=350 || (gesture==2&&now-touched<850) || std::abs(e.xbutton.x-pressX)+std::abs(e.xbutton.y-pressY)>12;
                    gesture=pat?2:tapCount>=3?3:1;touched=last=now;pressAt=-1;next=0;
                }
                if(e.type==KeyPress){
                    last=ms();char b[32];KeySym key;int n=XLookupString(&e.xkey,b,sizeof b,&key,nullptr);
                    if(settings){if(key==XK_Escape){settings=false;next=0;}continue;}
                    if(key==XK_Escape){if(typing)typing=false;else if(companion)companion=false;else if(bot.active())bot.stop();else running=false;}
                    else if(key==XK_F11)f.toggle();
                    else if(key==XK_F2){companion=!companion;typing=false;}
                    else if(key==XK_Return||key==XK_KP_Enter){
                        send();
                    }else if(key==XK_BackSpace){if(!input.empty())input.pop_back();}
                    else for(int i=0;i<n&&input.size()<140;i++)if((unsigned char)b[i]>=32&&(unsigned char)b[i]<127){input+=b[i];if(!typing)companion=false;}
                }
            }
            bool wasActive=bot.active();bot.update();int64_t now=ms();
            if(wasActive&&!bot.active()){
                if(promptMood==Neutral)mood=replyMood(bot.reply);
                moodUntil=now+5000;last=now;
            }else if(bot.active()&&bot.received&&promptMood==Neutral){mood=replyMood(bot.reply);}
            if(!bot.active()&&moodUntil&&now>=moodUntil){mood=Neutral;moodUntil=0;}
            if(pressAt>=0&&now-pressAt>=350){gesture=2;touched=last=now;}
            hinge.update(now);
            if(f.prefs.autoHinge&&hinge.tablet>=0&&hinge.tablet!=seenTablet){seenTablet=hinge.tablet;companion=seenTablet==1;typing=false;forcedSleep=false;last=now;next=0;}
            if(hinge.lid!=1&&now>=next){f.draw(now,last,touched,bot,input,companion,typing,shift,symbols,forcedSleep,gesture,hinge.fd>=0,settings,behavior,mood);next=now+(bot.active()?100:typing?150:67);}
            else if(hinge.lid==1)next=now+250;
            struct pollfd p[2]={{ConnectionNumber(d),POLLIN,0},{bot.fd,POLLIN,0}};
            poll(p,bot.active()?2:1,clamp(int(next-ms()),0,100));
        }
    }
    XCloseDisplay(d);return 0;
}
