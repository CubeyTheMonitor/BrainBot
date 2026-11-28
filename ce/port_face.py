"""Build the CE edition from the actual Brainux face, replacing OS-specific code."""
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
ce=Path(sys.argv[2]).read_text()
def sub(a,b):
    global s
    assert a in s, a
    s=s.replace(a,b,1)
def between(a,b,new):
    global s
    i=s.index(a);j=s.index(b,i)
    s=s[:i]+new+s[j:]
between("// RELAY native X11", "static int clamp", r'''// Generated from relay_face.cpp: native Windows CE GDI edition.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <wchar.h>
#include "llm.h"
#ifdef RELAY_FLITE
#include "flite.h"
extern "C" cst_voice* register_cmu_us_kal(const char*);
#endif
static int64_t ms(){
    static DWORD prev=GetTickCount();static uint64_t ticks=0;
    DWORD now=GetTickCount();ticks+=(DWORD)(now-prev);prev=now;return (int64_t)ticks;
}
static WCHAR appDir[MAX_PATH]={};
static std::wstring appPath(const WCHAR* name){return std::wstring(appDir)+name;}
''')
# Keep the verified V3 model loader and inference; expose it to the shared face.
a=ce.index("static Transformer model=");b=ce.index("static void send()",a)
engine=ce[a:b]
engine=engine.replace("static WCHAR appDir[MAX_PATH]={};","")
engine=engine.replace("0.65f,0.9f,GetTickCount()","modelTemp/100.0f,0.9f,GetTickCount()")
engine=engine.replace("if(n<1||n>52)","if(n<1||n>52)")
between("// Read switch states", "struct Preferences", r'''
static CRITICAL_SECTION replyLock;
static volatile LONG cancelWork=0;
static int modelTemp=65;
static std::string request,response;
'''+engine+r'''
struct Child {
    HANDLE thread=NULL;bool received=false;std::string reply;
    bool active()const{return thread!=NULL;}
    bool start(const std::string& message,int temperature){
        if(active())return false;
        request=message;modelTemp=temperature;InterlockedExchange(&cancelWork,0);
        setResponse("");reply.clear();received=false;
        thread=CreateThread(NULL,0,generate,NULL,0,NULL);
        return thread!=NULL;
    }
    void update(){
        if(!thread)return;
        reply=getResponse();received=!reply.empty();
        if(WaitForSingleObject(thread,0)==WAIT_OBJECT_0){
            reply=getResponse();received=!reply.empty();CloseHandle(thread);thread=NULL;
        }
    }
    void stop(){if(thread)InterlockedExchange(&cancelWork,1);}
    ~Child(){if(thread){stop();WaitForSingleObject(thread,INFINITE);CloseHandle(thread);}}
};
static HWAVEOUT wave=NULL;
static WAVEHDR audioHeader={};
static short samples[1600];
#ifdef RELAY_FLITE
static cst_voice* speechVoice=NULL;
static cst_wave* speechWave=NULL;
static HANDLE speechThread=NULL;
static std::string speechText;
static bool speechDiscard=false;
static DWORD WINAPI synthesize(LPVOID){
    if(!speechVoice){flite_init();speechVoice=register_cmu_us_kal(NULL);}
    if(speechVoice)speechWave=flite_text_to_wave(speechText.c_str(),speechVoice);
    return 0;
}
static void startSpeech(const std::string& text){
    if(speechThread||text.empty()||wave)return;
    speechText=text.substr(0,400);speechDiscard=false;
    speechThread=CreateThread(NULL,0,synthesize,NULL,0,NULL);
}
#endif
static void closeAudio(){
    if(wave){waveOutReset(wave);if(audioHeader.dwFlags&WHDR_PREPARED)waveOutUnprepareHeader(wave,&audioHeader,sizeof audioHeader);waveOutClose(wave);wave=NULL;}
#ifdef RELAY_FLITE
    if(speechThread)speechDiscard=true;
    else if(speechWave){delete_wave(speechWave);speechWave=NULL;}
#endif
}
#ifdef RELAY_FLITE
static void updateSpeech(){
    if(!speechThread||WaitForSingleObject(speechThread,0)!=WAIT_OBJECT_0)return;
    CloseHandle(speechThread);speechThread=NULL;
    if(speechDiscard||!speechWave||speechWave->num_samples<=0||speechWave->num_channels!=1){closeAudio();return;}
    WAVEFORMATEX fmt={};fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=1;
    fmt.nSamplesPerSec=speechWave->sample_rate;fmt.wBitsPerSample=16;fmt.nBlockAlign=2;fmt.nAvgBytesPerSec=fmt.nSamplesPerSec*2;
    for(int i=0;i<speechWave->num_samples;i++)speechWave->samples[i]/=3;
    if(waveOutOpen(&wave,WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){wave=NULL;closeAudio();return;}
    ZeroMemory(&audioHeader,sizeof audioHeader);audioHeader.lpData=(LPSTR)speechWave->samples;audioHeader.dwBufferLength=speechWave->num_samples*2;
    if(waveOutPrepareHeader(wave,&audioHeader,sizeof audioHeader)!=MMSYSERR_NOERROR||waveOutWrite(wave,&audioHeader,sizeof audioHeader)!=MMSYSERR_NOERROR)closeAudio();
}
static void finishSpeech(){if(speechThread){speechDiscard=true;WaitForSingleObject(speechThread,INFINITE);updateSpeech();}closeAudio();}
static int nextSound(int n){return (n+1)%3;}
#else
static int nextSound(int n){return !n;}
#endif
static const char* soundName(int n){
#ifdef RELAY_FLITE
    if(n==2)return "Voice";
#endif
    return n?"Tone On":"Sound Off";
}
static int validSound(int n){
#ifdef RELAY_FLITE
    return clamp(n,0,2);
#else
    return n==1;
#endif
}
static void quietTone(Mood mood){
    if(wave)return;
    WAVEFORMATEX fmt={};fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=1;fmt.nSamplesPerSec=16000;fmt.wBitsPerSample=16;fmt.nBlockAlign=2;fmt.nAvgBytesPerSec=32000;
    if(waveOutOpen(&wave,WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){wave=NULL;return;}
    for(int i=0;i<1600;i++){
        int period=mood==Annoyed?65:mood==Happy?26:36,phase=i%period;
        int amp=std::min(500,std::min(i*5,(1599-i)*5));
        samples[i]=(short)(amp*(phase<period/2?phase*4-period:3*period-phase*4)/period);
    }
    ZeroMemory(&audioHeader,sizeof audioHeader);audioHeader.lpData=(LPSTR)samples;audioHeader.dwBufferLength=sizeof samples;
    if(waveOutPrepareHeader(wave,&audioHeader,sizeof audioHeader)!=MMSYSERR_NOERROR||waveOutWrite(wave,&audioHeader,sizeof audioHeader)!=MMSYSERR_NOERROR)closeAudio();
}
''')
between("struct Preferences", "struct Face", r'''
struct Preferences {
    int style=0,palette=1,effect=0,sleep=45,tracking=1,autoHinge=0,temp=65,sound=0,clean=1,battery=0;
    void load(){
        HANDLE h=CreateFileW(appPath(L"relay.conf").c_str(),GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
        if(h==INVALID_HANDLE_VALUE)return;
        char data[512]={};DWORD n=0;BOOL ok=ReadFile(h,data,sizeof(data)-1,&n,NULL);CloseHandle(h);if(!ok)return;
        std::string all(data,n);size_t p=0;
        while(p<all.size()){
            size_t e=all.find('\n',p);if(e==std::string::npos)e=all.size();
            std::string line=all.substr(p,e-p);size_t sep=line.find(' ');
            if(sep!=std::string::npos){
                std::string k=line.substr(0,sep);int v=atoi(line.c_str()+sep+1);
                if(k=="style")style=clamp(v,0,4);else if(k=="palette")palette=clamp(v,0,5);
                else if(k=="effect")effect=clamp(v,0,3);else if(k=="sleep")sleep=clamp(v,0,120);
                else if(k=="tracking")tracking=v!=0;else if(k=="temp")temp=clamp(v,10,120);
                else if(k=="sound")sound=validSound(v);else if(k=="clean")clean=v!=0;else if(k=="battery")battery=v!=0;
            }
            p=e+1;
        }
    }
    bool save()const{
        char data[256];int n=sprintf(data,"style %d\npalette %d\neffect %d\nsleep %d\ntracking %d\ntemp %d\nsound %d\nclean %d\nbattery %d\n",style,palette,effect,sleep,tracking,temp,sound,clean,battery);
        std::wstring dest=appPath(L"relay.conf"),tmp=appPath(L"relay.conf.tmp"),backup=appPath(L"relay.conf.bak");
        HANDLE h=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if(h==INVALID_HANDLE_VALUE)return false;
        DWORD wrote=0;BOOL ok=WriteFile(h,data,n,&wrote,NULL);if(ok)ok=FlushFileBuffers(h);CloseHandle(h);
        if(!ok||wrote!=(DWORD)n)return false;
        DeleteFileW(backup.c_str());
        bool had=GetFileAttributesW(dest.c_str())!=INVALID_FILE_ATTRIBUTES;
        if(had&&!MoveFileW(dest.c_str(),backup.c_str()))return false;
        if(!MoveFileW(tmp.c_str(),dest.c_str())){if(had)MoveFileW(backup.c_str(),dest.c_str());return false;}
        DeleteFileW(backup.c_str());return true;
    }
};
// Query only; never change the power manager or Sharp's battery process.
static FARPROC powerLookup(FARPROC (WINAPI *fn)(HMODULE,LPCSTR),HMODULE h){return fn(h,"GetSystemPowerStatusEx2");}
static FARPROC powerLookup(FARPROC (WINAPI *fn)(HMODULE,LPCWSTR),HMODULE h){return fn(h,L"GetSystemPowerStatusEx2");}
static std::string batteryText(){
    typedef DWORD (WINAPI *Query)(void*,DWORD,BOOL);
    static Query query=(Query)powerLookup(&GetProcAddress,GetModuleHandle(L"coredll.dll"));
    static int64_t next=-1;static std::string cached="BAT --";
    int64_t now=ms();
    if(now<next)return cached;next=now+10000;
    union {DWORD align;BYTE b[128];} status={};
    if(!query||query(status.b,sizeof(status.b),FALSE)<3){cached="BAT --";return cached;}
    BYTE percent=status.b[2],flag=status.b[1];
    if(percent>100)cached="BAT --";
    else{char buf[24];sprintf(buf,"BAT %u%%",(unsigned)percent);cached=buf;}
    if(flag!=255&&(flag&8))cached+=" +";
    return cached;
}
static int stylusX=-1,stylusY=-1;
static int64_t stylusMoved=-100000;
struct NativeEvent{UINT type;int x,y;WPARAM key;};
static std::vector<NativeEvent> events;
static LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    int x=(short)LOWORD(lp),y=(short)HIWORD(lp);
    switch(msg){
        case WM_ACTIVATE:events.push_back({msg,0,0,wp});return 0;
        case WM_CLOSE:events.push_back({msg,0,0,0});return 0;
        case WM_SIZE:events.push_back({msg,(int)LOWORD(lp),(int)HIWORD(lp),0});return 0;
        case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_LBUTTONDOWN:SetCapture(hwnd);events.push_back({msg,x,y,0});return 0;
        case WM_LBUTTONUP:ReleaseCapture();events.push_back({msg,x,y,0});return 0;
        case WM_MOUSEMOVE:events.push_back({msg,x,y,0});return 0;
        case WM_CHAR:events.push_back({msg,0,0,wp});return 0;
        case WM_KEYDOWN:if(wp==VK_ESCAPE||wp==VK_F2||wp==VK_F11)events.push_back({msg,0,0,wp});return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}
''')
sub("    Display* d; Window win; GC gc; Pixmap pix=0; XFontStruct* font;\n    int w=480,h=272; int depth; bool fullscreen=false;",
    "    HWND win=NULL; HDC pix=NULL; HBITMAP bitmap=NULL; HGDIOBJ oldBitmap=NULL; HFONT font=NULL; HGDIOBJ oldFont=NULL;\n    COLORREF ink=0; int w=480,h=272; bool fullscreen=true;")
sub("    Pixmap fontAtlas=0;\n","")
between("    Face(Display* display)", "    void applyPalette()", r'''    Face(HINSTANCE instance){
        WNDCLASS wc={};wc.hInstance=instance;wc.lpszClassName=L"RelayCEParity";wc.lpfnWndProc=windowProc;wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        if(!RegisterClass(&wc))return;
        win=CreateWindowEx(0,wc.lpszClassName,L"RELAY CE",WS_OVERLAPPED|WS_MAXIMIZE,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),NULL,NULL,instance,NULL);
        if(!win)return;
        LOGFONT lf={};lf.lfHeight=-17;lf.lfWeight=FW_NORMAL;lf.lfPitchAndFamily=FIXED_PITCH|FF_MODERN;lstrcpy(lf.lfFaceName,L"Courier New");
        font=CreateFontIndirect(&lf);
        prefs.load();applyPalette();ShowWindow(win,SW_SHOW);applyFullscreen();UpdateWindow(win);
        RECT r;GetClientRect(win,&r);resize(r.right,r.bottom);
    }
    ~Face(){freeBuffer();if(font)DeleteObject(font);if(win)DestroyWindow(win);}
    void freeBuffer(){if(pix){if(oldFont)SelectObject(pix,oldFont);if(oldBitmap)SelectObject(pix,oldBitmap);DeleteDC(pix);}if(bitmap)DeleteObject(bitmap);pix=NULL;bitmap=NULL;oldBitmap=NULL;oldFont=NULL;}
    unsigned long color(const char* name){unsigned long rgb=strtoul(name+1,NULL,16);return RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);}
''')
sub("        if(paletteReady){unsigned long old[]={black,outer,outline,inner,green,highlight,muted,sweepColor,scanColor};XFreeColors(d,DefaultColormap(d,DefaultScreen(d)),old,9,0);}\n","")
sub("        if(pix)XFreePixmap(d,pix);pix=XCreatePixmap(d,win,w,h,depth);",r'''        freeBuffer();HDC dc=GetDC(win);if(!dc)return;
        pix=CreateCompatibleDC(dc);bitmap=pix?CreateCompatibleBitmap(dc,w,h):NULL;ReleaseDC(win,dc);
        if(!pix||!bitmap){freeBuffer();return;}
        oldBitmap=SelectObject(pix,bitmap);if(font)oldFont=SelectObject(pix,font);
        TEXTMETRIC tm={};if(GetTextMetrics(pix,&tm)){fontW=std::max(1,(int)tm.tmAveCharWidth);fontH=std::max(1,(int)tm.tmHeight);fontAscent=tm.tmAscent;}
''')
between("    void fg(", "    void wrap(", r'''    void fg(unsigned long c){ink=(COLORREF)c;}
    void rawRect(int x,int y,int a,int b,unsigned long c){
        if(a<=0||b<=0)return;RECT r={x,y,x+a,y+b};HBRUSH brush=CreateSolidBrush((COLORREF)c);if(brush){FillRect(pix,&r,brush);DeleteObject(brush);}
    }
    void rawLine(int x,int y,int xx,int yy,unsigned long c,int thick=1){
        if(y==yy){rawRect(std::min(x,xx),y-thick/2,std::abs(xx-x)+1,thick,c);return;}
        if(x==xx){rawRect(x-thick/2,std::min(y,yy),thick,std::abs(yy-y)+1,c);return;}
        int n=std::max(std::abs(xx-x),std::abs(yy-y));for(int i=0;i<=n;i++)rawRect(x+(xx-x)*i/n,y+(yy-y)*i/n,thick,thick,c);
    }
    void rect(int x,int y,int a,int b,unsigned long c){if(a>0&&b>0)rawRect(sx(x),sy(y),std::max(1,dim(a)),std::max(1,dim(b)),c);}
    void line(int x,int y,int a,int b,unsigned long c,int thick=2){rawLine(sx(x),sy(y),sx(a),sy(b),c,std::max(1,dim(thick)));}
    void text(int x,int y,const std::string& s,unsigned long c){
        std::wstring ws;for(unsigned char ch:s)ws+=(WCHAR)(ch>=32&&ch<127?ch:' ');
        RECT r={x,y-fontAscent,w,std::max(y-fontAscent+fontH+2,y+2)};
        SetTextColor(pix,(COLORREF)c);SetBkMode(pix,TRANSPARENT);DrawText(pix,ws.c_str(),(int)ws.size(),&r,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
    }
    void applyFullscreen(){
        if(!win)return;
        // Change only RELAY's own Z order. Do not kill/hide any system window.
        if(fullscreen&&prefs.clean){
            SetWindowPos(win,HWND_TOPMOST,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),SWP_NOACTIVATE);
        }else SetWindowPos(win,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }
    void present(){HDC dc=GetDC(win);if(dc){BitBlt(dc,0,0,w,h,pix,0,0,SRCCOPY);ReleaseDC(win,dc);}}
''')
sub("            fg(outline);XSetLineAttributes(d,gc,1,LineSolid,CapButt,JoinMiter);\n            XDrawRectangle(d,pix,gc,x+1,y+1,ww-3,hh-3);",
    "            rawLine(x+1,y+1,x+ww-2,y+1,outline);rawLine(x+1,y+hh-2,x+ww-2,y+hh-2,outline);rawLine(x+1,y+1,x+1,y+hh-2,outline);rawLine(x+ww-2,y+1,x+ww-2,y+hh-2,outline);")
between("            std::vector<XRectangle> dots;", "        }else if(prefs.style==3)",r'''            int rows=clamp((height+12)/14,1,7);
            for(int r=0;r<rows;r++)for(int c=0;c<7;c++){
                if(rows>4&&(r==0||r==rows-1)&&(c==0||c==6))continue;
                rect(x-46+c*14+dx/2,y-rows*7+r*14+dy/2,10,10,green);
            }
''')
between("            std::vector<XSegment> lines;", "        }\n        if(prefs.effect&2)", "            for(int y=faceTop+4;y<faceBottom;y+=6)rawLine(0,y,w-1,y,scanColor);\n")
sub("                fg(sweepColor);XFillRectangle(d,pix,gc,0,y,w,1);","                rawRect(0,y,w,1,sweepColor);")
sub('row("Auto fold",prefs.autoHinge?"On":"Off","autohinge");','keyRow(y,hh,{{soundName(prefs.sound),"sound"},{prefs.clean?"Clean On":"Clean Off","clean"},{prefs.battery?"Batt On":"Batt Off","battery"}});y+=hh;')
between("    void toggle(){","    void draw(",r'''    void toggle(){fullscreen=!fullscreen;ShowWindow(win,fullscreen?SW_MAXIMIZE:SW_RESTORE);applyFullscreen();}
''')
sub("        keys.clear();\n        fg(black);XFillRectangle(d,pix,gc,0,0,w,h);","        if(!pix)return;keys.clear();rawRect(0,0,w,h,black);")
s=s.replace("XCopyArea(d,pix,win,gc,0,0,w,h,0,0);XFlush(d);","present();")
between("        Window root,child;", "        int eye=0;",r'''        int px=stylusX,py=stylusY;bool pointer=px>=0&&py>=0;pointerMoved=stylusMoved;
''')
sub("            fg(outline);XSetLineAttributes(d,gc,1,LineSolid,CapButt,JoinMiter);\n            XDrawLine(d,pix,gc,8,inputLine,w-8,inputLine);","            rawLine(8,inputLine,w-8,inputLine,outline);")
sub("                fg(outline);XSetLineAttributes(d,gc,1,LineSolid,CapButt,JoinMiter);\n                XDrawLine(d,pix,gc,8,top,w-8,top);","                rawLine(8,top,w-8,top,outline);")
sub("        int height=sleepy?23:(react&&gesture==1)?102:","        int height=sleepy?23:(react&&gesture==1)?116:")
# Keyboard may be opened from either mode. Preserve existing layout and toolbar.
sub("if(!settings&&companion&&typing)","if(!settings&&typing)")
sub('            text(10,h-6-(fontH-fontAscent),"> "+shown+(bot.active()?"":"_"),green);',
    '            text(10,h-6-(fontH-fontAscent),"> "+shown+(bot.active()?"":"_"),green);keys.push_back({0,inputLine,w,h-inputLine,"type"});')
# Reuse common event actions, not the X11-specific event loop.
a=s.index("int main(")
s=s[:a]+r'''
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    InitializeCriticalSection(&replyLock);
    GetModuleFileNameW(NULL,appDir,MAX_PATH);appDir[MAX_PATH-1]=0;WCHAR* slash=wcsrchr(appDir,L'\\');if(slash)slash[1]=0;else appDir[0]=0;
    {
        Face f(instance);if(!f.win){DeleteCriticalSection(&replyLock);return 2;}
        Child bot;std::string input;
        bool running=true,companion=false,typing=false,shift=false,symbols=false,forcedSleep=false,settings=false,behavior=false;
        bool stroking=false,tonePlayed=false;
        int gesture=0,tapCount=0;int64_t pressAt=-1,lastPoke=-10000;int pressX=0,pressY=0;
        int64_t last=ms(),touched=-10000,next=0,moodUntil=0;
        Mood mood=Neutral,promptMood=Neutral;
        auto send=[&](){
            if(!bot.active()&&input.find_first_not_of(" \t\r\n")!=std::string::npos){
                if(bot.start(input,f.prefs.temp)){closeAudio();promptMood=mood=userMood(input);moodUntil=0;input.clear();typing=shift=symbols=forcedSleep=false;gesture=0;touched=-10000;tonePlayed=false;}
                else bot.reply="Could not start model.";
                last=ms();next=0;
            }
        };
        while(running){
            MSG m={};while(PeekMessage(&m,NULL,0,0,PM_REMOVE)){if(m.message==WM_QUIT){running=false;break;}TranslateMessage(&m);DispatchMessage(&m);}
            std::vector<NativeEvent> batch;batch.swap(events);
            for(const auto& e:batch){
                int64_t now=ms();
                if(e.type==WM_CLOSE){running=false;break;}
                if(e.type==WM_ACTIVATE){if(LOWORD(e.key)!=WA_INACTIVE)f.applyFullscreen();else SetWindowPos(f.win,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
                if(e.type==WM_SIZE&&e.x>0&&e.y>0&&(e.x!=f.w||e.y!=f.h))f.resize(e.x,e.y);
                if(e.type==WM_LBUTTONDOWN||e.type==WM_MOUSEMOVE||e.type==WM_LBUTTONUP){
                    stylusX=e.x;stylusY=e.y;stylusMoved=now;
                }
                if(e.type==WM_LBUTTONDOWN){
                    last=now;std::string action=f.hit(e.x,e.y);pressAt=-1;
                    if(action=="settings"){settings=!settings;behavior=false;typing=false;}
                    else if(settings){
                        bool changed=true;
                        if(action=="style")f.prefs.style=(f.prefs.style+1)%5;
                        else if(action=="palette"){f.prefs.palette=(f.prefs.palette+1)%6;f.applyPalette();}
                        else if(action=="effect")f.prefs.effect=(f.prefs.effect+1)%4;
                        else if(action=="tracking")f.prefs.tracking=!f.prefs.tracking;
                        else if(action=="sound"){closeAudio();f.prefs.sound=nextSound(f.prefs.sound);}
                        else if(action=="clean"){f.prefs.clean=!f.prefs.clean;f.applyFullscreen();}
                        else if(action=="battery")f.prefs.battery=!f.prefs.battery;
                        else if(action=="sleeptime"){int& t=f.prefs.sleep;t=t==0?15:t==15?45:t==45?120:0;}
                        else if(action=="temperature"){int& t=f.prefs.temp;t=t<40?40:t<65?65:t<85?85:t<100?100:20;}
                        else if(action=="defaults"){f.prefs=Preferences{};f.applyPalette();f.applyFullscreen();closeAudio();}
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
                    else if(action=="stop"){bot.stop();mood=promptMood=Neutral;moodUntil=0;closeAudio();}
                    else if(action=="sleep")forcedSleep=!forcedSleep;
                    else if(action=="mode"){companion=!companion;typing=false;forcedSleep=false;}
                    else if(action=="exit"){running=false;break;}
                    else if(action.compare(0,5,"char:")==0){if(input.size()<140)input+=action.substr(5);shift=false;}
                    else if(!typing){forcedSleep=false;pressAt=now;pressX=e.x;pressY=e.y;stroking=false;gesture=1;touched=now;}
                    next=0;
                }
                if(e.type==WM_MOUSEMOVE&&pressAt>=0){
                    last=now;if(now-pressAt>=180&&std::abs(e.x-pressX)+std::abs(e.y-pressY)>=std::max(32,f.w/15)){stroking=true;gesture=2;touched=now;next=0;}
                }
                if(e.type==WM_LBUTTONUP&&pressAt>=0){
                    bool pat=stroking||now-pressAt>=650||(now-pressAt>=180&&std::abs(e.x-pressX)+std::abs(e.y-pressY)>=std::max(32,f.w/15));
                    if(pat){tapCount=0;gesture=2;}else{tapCount=now-lastPoke<900?tapCount+1:1;lastPoke=now;gesture=tapCount>=3?3:1;}
                    touched=last=now;pressAt=-1;next=0;
                }
                if(e.type==WM_KEYDOWN){
                    last=now;
                    if(e.key==VK_ESCAPE){if(settings)settings=false;else if(typing)typing=false;else if(companion)companion=false;else if(bot.active())bot.stop();else running=false;}
                    else if(e.key==VK_F11)f.toggle();
                    else if(e.key==VK_F2){companion=!companion;typing=false;}
                    next=0;
                }
                if(e.type==WM_CHAR&&!settings){
                    last=now;forcedSleep=false;
                    if(e.key==13)send();
                    else if(e.key==8){if(!input.empty())input.pop_back();}
                    else if(e.key>=32&&e.key<127&&input.size()<140){input+=(char)e.key;if(!typing)companion=false;}
                    next=0;
                }
            }
            bool wasActive=bot.active();bot.update();int64_t now=ms();
            if(wasActive&&!bot.active()){if(promptMood==Neutral)mood=replyMood(bot.reply);moodUntil=now+5000;last=now;}
            else if(bot.active()&&bot.received&&promptMood==Neutral)mood=replyMood(bot.reply);
            if(wave&&(audioHeader.dwFlags&WHDR_DONE))closeAudio();
            if(f.prefs.sound==1&&!tonePlayed&&bot.received){tonePlayed=true;quietTone(mood);}
#ifdef RELAY_FLITE
            updateSpeech();
            if(f.prefs.sound==2&&wasActive&&!bot.active()&&!tonePlayed&&!cancelled()&&bot.received){tonePlayed=true;startSpeech(bot.reply);}
#endif
            if(!bot.active()&&moodUntil&&now>=moodUntil){mood=Neutral;moodUntil=0;}
            if(pressAt>=0&&now-pressAt>=650){gesture=2;touched=last=now;}
            if(now>=next){f.draw(now,last,touched,bot,input,companion,typing,shift,symbols,forcedSleep,gesture,false,settings,behavior,mood);next=now+(bot.active()?100:typing?150:67);}
            Sleep((DWORD)clamp((int)(next-ms()),5,50));
        }
        closeAudio();bot.stop();
#ifdef RELAY_FLITE
        finishSpeech();
#endif
    }
    DeleteCriticalSection(&replyLock);return 0;
}
'''
# Animate the existing mouth waveform during native speech playback too.
sub('talking=bot.active()&&bot.received;','talking=(bot.active()&&bot.received)||wave!=NULL;')
# Optional palette-matched battery text, in both modes. Unknown values stay unknown.
sub('std::string state=thinking?"THINKING":talking?"TALKING":sleepy?"SLEEPY":hingeAvailable?"ONLINE":"MANUAL";','std::string state=thinking?"THINKING":talking?"TALKING":sleepy?"SLEEPY":"CE";if(prefs.battery)state=batteryText();')
sub('        if(companion){\n            if(rows){','        if(companion){\n            if(prefs.battery){std::string b=batteryText();text(std::max(10,w-int(b.size())*fontW-8),fontAscent+2,b,muted);}\n            if(rows){')
# Extend casual spelling without changing any LLM response.
s=s.replace('"you look stupid","you are stupid"','"you look stupid","u look stupid","you are stupid"').replace('"you look cute","you\\x27re cute"','"you look cute","u look cute","you\\x27re cute"')
s=s.replace("std::to_string(", "ceInt(")
s=s.replace("static int clamp(", "static std::string ceInt(int n){char b[32];sprintf(b,\"%d\",n);return b;}\nstatic int clamp(",1)
assert "XCopyArea" not in s and "XDraw" not in s and "XFill" not in s and "XSet" not in s
Path(sys.argv[3]).write_text(s)
