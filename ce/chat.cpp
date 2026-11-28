// RELAY CE: native GDI, WaveOut and TinyTalk V3 inference.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <mmsystem.h>
#include <tchar.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <string>
#include <vector>
#include "llm.h"
static DWORD started, touched, pressedAt, lastActivity, gestureAt, lastPoke, moodAt;
static int downX,downY,pokeCount=0,gesture=0;
static bool stroking=false,asleep=false,settings=false,tracking=true,scanlines=false;
static int sleepSeconds=45;
static bool pressed=false;
static int pointerX=-1, pointerY=-1, lastKey=0, palette=0;
static bool talking=false;
static HWAVEOUT wave=NULL;
static WAVEHDR header={};
static short samples[4000];
static LPCTSTR audioStatus=TEXT("Sound not tested");
static RECT soundButton, colorButton, exitButton, sendButton, typeButton;
static CRITICAL_SECTION replyLock;
static HANDLE worker=NULL;
static volatile LONG cancelWork=0;
static bool closing=false,keyboard=false,shift=false,soundOn=false;
static bool replyTone=false;
static std::string input,request,response;
static int mood=0;
static Transformer model={};
static Tokenizer tokenizer={};
static bool loaded=false,loadFailed=false;
static std::vector<uint8_t> modelStorage,tokenStorage;
static WCHAR appDir[MAX_PATH]={};
static bool cancelled(){return InterlockedCompareExchange(&cancelWork,0,0)!=0;}
static void setResponse(const std::string& r){EnterCriticalSection(&replyLock);response=r;LeaveCriticalSection(&replyLock);}
static std::string getResponse(){EnterCriticalSection(&replyLock);std::string r=response;LeaveCriticalSection(&replyLock);return r;}
static bool readBlob(const WCHAR* file,std::vector<uint8_t>& dst,bool aligned,uint8_t** data,DWORD* size){
    std::wstring path=std::wstring(appDir)+file;
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE)return false;
    DWORD n=GetFileSize(h,NULL);
    if(n==INVALID_FILE_SIZE||n<20||n>16*1024*1024){CloseHandle(h);return false;}
    try{dst.resize(n+(aligned?15:0));}catch(...){CloseHandle(h);return false;}
    *data=dst.data();
    if(aligned)*data=(uint8_t*)(((uintptr_t)*data+15)&~(uintptr_t)15);
    DWORD got=0;BOOL ok=ReadFile(h,*data,n,&got,NULL);CloseHandle(h);*size=n;
    return ok&&got==n;
}
static DWORD WINAPI generate(LPVOID){
    if(!loaded){
        if(loadFailed){setResponse("Model failed to load. Restart and check both model files.");return 1;}
        uint8_t *m=NULL,*t=NULL;DWORD mn=0,tn=0;
        if(!readBlob(L"model_neo_q4.bin",modelStorage,true,&m,&mn)||
           !readBlob(L"tok_neo.bin",tokenStorage,false,&t,&tn)){
            setResponse("Missing or unreadable model files. Check the app folder.");return 1;
        }
        int cfg[7]={};uint32_t version=0;
        if(mn>=64){memcpy(&version,m+4,4);memcpy(cfg,m+8,sizeof cfg);}
        if(mn<64||memcmp(m,"CRDP",4)||version!=3||cfg[0]!=256||cfg[1]<=0||cfg[1]>2048||cfg[2]!=8||cfg[3]!=16||cfg[4]!=16||cfg[5]<=0||cfg[5]>50257||cfg[6]<72||cfg[6]>2048||m[38]!=ARCH_GPTNEO){
            setResponse("Incompatible model file.");return 1;
        }
        if(!llm_init_embedded(&model,m,mn,72)||!llm_tokenizer_from_memory(&tokenizer,t,tn,model.config.vocab_size)){
            loadFailed=true;setResponse("Model initialization failed. Restart RELAY.");return 1;
        }
        loaded=true;
    }
    std::string prompt="User: "+request+"\nBot:";
    std::vector<int> tokens(prompt.size()+16);int n=0;
    llm_encode(&tokenizer,prompt.c_str(),1,0,tokens.data(),&n);
    if(n<1||n>52){setResponse("That message is too long for my tiny memory.");return 0;}
    Sampler sampler={};llm_build_sampler(&sampler,model.config.vocab_size,0.65f,0.9f,GetTickCount());
    float* logits=NULL;
    for(int pos=0;pos<n&&!cancelled();pos++)logits=llm_forward(&model,tokens[pos],pos);
    std::string out;int previous=tokens[n-1];
    for(int i=0;i<20&&!cancelled()&&logits;i++){
        int next=llm_sample(&sampler,logits);
        if(next==tokenizer.eos_id)break;
        char scratch[256];const char* piece=llm_decode(&tokenizer,previous,next,scratch,sizeof scratch);
        if(piece)out+=piece;
        size_t end=out.find("\nUser:");if(end!=std::string::npos){out.resize(end);break;}
        setResponse(out);previous=next;
        if(i<19)logits=llm_forward(&model,next,n+i);
    }
    if(cancelled()&&out.empty())setResponse("Stopped.");
    else if(out.empty())setResponse("No reply. Try again.");
    else setResponse(out);
    return 0;
}
static void send(){
    if(worker||input.find_first_not_of(' ')==std::string::npos)return;
    request=input;input.clear();keyboard=false;setResponse("");
    gesture=0;pokeCount=0;asleep=false;lastActivity=GetTickCount();moodAt=lastActivity;replyTone=false;
    std::string lower=request;for(char& c:lower)if(c>='A'&&c<='Z')c+=32;
    mood=lower.find("you look stupid")!=std::string::npos||lower.find("you look ugly")!=std::string::npos?2:lower.find("you look cute")!=std::string::npos||lower.find("u look cute")!=std::string::npos||lower.find("good job")!=std::string::npos?1:0;
    InterlockedExchange(&cancelWork,0);
    worker=CreateThread(NULL,0,generate,NULL,0,NULL);
    if(!worker)setResponse("Could not start inference thread.");
}
static std::wstring wide(const std::string& s){
    std::wstring out;for(unsigned char c:s)out+=(WCHAR)(c>=32&&c<127?c:' ');
    return out;
}
static void requestClose(HWND hwnd){
    if(worker){closing=true;InterlockedExchange(&cancelWork,1);}
    else DestroyWindow(hwnd);
}
static int min2(int a,int b){return a<b?a:b;}
static int max2(int a,int b){return a>b?a:b;}
static int clamp(int v,int a,int b){return max2(a,min2(v,b));}
static bool in(RECT r,int x,int y){return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;}
static void box(HDC dc,int x,int y,int w,int h,COLORREF c){
    if(w<=0||h<=0)return;
    RECT r={x,y,x+w,y+h};HBRUSH b=CreateSolidBrush(c);
    if(b){FillRect(dc,&r,b);DeleteObject(b);}
}
static void label(HDC dc,RECT r,LPCTSTR s,COLORREF c){
    SetTextColor(dc,c);SetBkMode(dc,TRANSPARENT);
    DrawText(dc,s,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
}
static void closeAudio(){
    if(wave){
        waveOutReset(wave);
        if(header.dwFlags&WHDR_PREPARED)waveOutUnprepareHeader(wave,&header,sizeof(header));
        waveOutClose(wave);wave=NULL;
    }
}
static void beep(int tone=0){
    if(wave)return;
    WAVEFORMATEX fmt={};fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=1;
    fmt.nSamplesPerSec=16000;fmt.wBitsPerSample=16;fmt.nBlockAlign=2;fmt.nAvgBytesPerSec=32000;
    if(waveOutOpen(&wave,WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){
        wave=NULL;audioStatus=TEXT("Audio open failed");return;
    }
    for(int i=0;i<1600;i++){
        int period=tone==2?(i<800?65:81):tone==1?(i<800?26:19):(i<800?32:23);
        int amp=min2(550,min2(i*5,(1599-i)*5));
        int phase=i%period;int triangle=phase<period/2?phase*4-period:3*period-phase*4;
        samples[i]=short(amp*triangle/period);
    }
    ZeroMemory(&header,sizeof(header));header.lpData=(LPSTR)samples;header.dwBufferLength=1600*sizeof(short);
    if(waveOutPrepareHeader(wave,&header,sizeof(header))!=MMSYSERR_NOERROR ||
       waveOutWrite(wave,&header,sizeof(header))!=MMSYSERR_NOERROR){
        closeAudio();audioStatus=TEXT("Audio playback failed");return;
    }
    audioStatus=TEXT("Playing beep...");talking=true;
}
static void paint(HWND hwnd){
    PAINTSTRUCT ps;HDC screen=BeginPaint(hwnd,&ps);RECT client;
    GetClientRect(hwnd,&client);int w=client.right,h=client.bottom;
    if(w<1||h<1){EndPaint(hwnd,&ps);return;}
    HDC buffer=CreateCompatibleDC(screen);
    HBITMAP bitmap=buffer?CreateCompatibleBitmap(screen,w,h):NULL;
    HGDIOBJ old=bitmap?SelectObject(buffer,bitmap):NULL;
    HDC dc=bitmap?buffer:screen;
    const COLORREF backgrounds[]={RGB(8,20,10),RGB(41,32,8),RGB(174,183,155),RGB(6,18,32),RGB(30,5,5),RGB(14,14,14)};
    const COLORREF frames[]={RGB(53,80,57),RGB(120,96,37),RGB(126,137,109),RGB(35,80,110),RGB(100,32,32),RGB(75,75,75)};
    const COLORREF eyes[]={RGB(129,201,107),RGB(232,185,77),RGB(36,49,33),RGB(112,216,246),RGB(235,85,74),RGB(222,222,222)};
    COLORREF bg=backgrounds[palette],frame=frames[palette],eye=eyes[palette];
    box(dc,0,0,w,h,bg);
    DWORD now=GetTickCount(),elapsed=now-started;
    // A fresh tap or stroke replaces the previous gesture; idle pat cannot mask a poke.
    int reaction=pressed?(stroking||now-pressedAt>=350?1:2):((now-gestureAt<1400)?gesture:0);
    bool happy=reaction==1||(reaction==0&&mood==1);
    bool annoyed=reaction==3||(reaction==0&&mood==2);
    bool surprised=reaction==2;
    if(asleep){happy=false;annoyed=false;surprised=false;}
    std::string reply=getResponse();
    int unit=max2(1,min2(w/13,(h-100)/6)),cy=h/2-15;
    int eh=asleep?max2(2,unit/8):happy?unit:annoyed?max2(3,unit/2):surprised?unit*5/2:unit*2;
    if(!asleep&&elapsed%5000>4800)eh=max2(2,unit/4);
    for(int i=0;i<2;i++){
        int x=w*(i?3:1)/4, y=cy-unit/2;
        box(dc,x-unit*3/2,y-unit*3/2,unit*3,unit*3,frame);
        box(dc,x-unit*3/2+3,y-unit*3/2+3,unit*3-6,unit*3-6,bg);
        int dx=!tracking||asleep||pointerX<0?0:clamp((pointerX-x)/12,-unit/3,unit/3);
        int dy=!tracking||asleep||pointerY<0?0:clamp((pointerY-y)/16,-unit/5,unit/5);
        box(dc,x-unit*3/4+dx,y-eh/2+dy,unit*3/2,eh,eye);
    }
    int my=cy+unit*3/2,th=max2(2,unit/9);
    if(!asleep&&(talking||(worker&&!reply.empty()))){
        for(int i=0;i<9;i++){
            int bar=th+int((elapsed/70+i*3)%5)*th;
            box(dc,w/2-unit*3/2+i*unit/3,my-bar/2,max2(2,unit/4),bar,eye);
        }
    }else{
        box(dc,w/2-unit,my,unit*2,th,eye);
        if(happy){box(dc,w/2-unit,my-unit/3,th,unit/3,eye);box(dc,w/2+unit-th,my-unit/3,th,unit/3,eye);}
        if(surprised){box(dc,w/2-th,my-th,th*2,th*3,eye);}
        if(annoyed){box(dc,w/2-unit,my-th,th,th,eye);box(dc,w/2+unit-th,my-th,th,th,eye);}
    }
    if(scanlines)for(int y=28;y<h-104;y+=4)box(dc,0,y,w,1,bg);
    RECT title={0,2,w,25};label(dc,title,settings?TEXT("RELAY CE / SETTINGS"):asleep?TEXT("RELAY CE / SLEEPING"):worker?TEXT("RELAY CE / THINKING"):TEXT("RELAY CE / V3   [Settings]"),eye);
    if(settings){
        box(dc,0,26,w,h-26,bg);
        const LPCTSTR colors[]={TEXT("Green phosphor"),TEXT("Amber"),TEXT("Gray LCD"),TEXT("Blue VFD"),TEXT("Red LED"),TEXT("Mono CRT")};
        LPCTSTR names[]={colors[palette],tracking?TEXT("Eye tracking: ON"):TEXT("Eye tracking: OFF"),scanlines?TEXT("Scanlines: ON"):TEXT("Scanlines: OFF"),soundOn?TEXT("Quiet reply tone: ON"):TEXT("Quiet reply tone: OFF"),TEXT(""),TEXT("Sleep now"),TEXT("Done")};
        WCHAR sleepText[48];if(sleepSeconds)wsprintf(sleepText,TEXT("Auto sleep: %d seconds"),sleepSeconds);else lstrcpy(sleepText,TEXT("Auto sleep: OFF"));names[4]=sleepText;
        int rh=max2(1,(h-30)/7);
        for(int i=0;i<7;i++){RECT r={6,30+i*rh,w-6,30+(i+1)*rh};box(dc,r.left,r.top,r.right-r.left,r.bottom-r.top-2,frame);label(dc,r,names[i],eye);}
    }else if(keyboard){
        box(dc,0,26,w,h-26,bg);
        RECT edit={8,26,w-8,55};std::wstring text=wide("> "+input+"_");label(dc,edit,text.c_str(),eye);
        const char* rows[]={"1234567890","qwertyuiop","asdfghjkl","zxcvbnm,.?"};
        int kh=max2(1,(h-60)/5);
        for(int r=0;r<4;r++){
            int n=(int)strlen(rows[r]);
            for(int k=0;k<n;k++){
                RECT key={k*w/n,60+r*kh,(k+1)*w/n,60+(r+1)*kh};
                box(dc,key.left+1,key.top+1,key.right-key.left-2,kh-2,frame);
                WCHAR ch=(WCHAR)rows[r][k];if(shift&&ch>=L'a'&&ch<=L'z')ch-=32;
                WCHAR str[2]={ch,0};label(dc,key,str,eye);
            }
        }
        LPCTSTR names[]={TEXT("Shift"),TEXT("Space"),TEXT("Del"),TEXT("Send"),TEXT("Hide")};
        for(int k=0;k<5;k++){RECT key={k*w/5,60+4*kh,(k+1)*w/5,h};box(dc,key.left+1,key.top+1,key.right-key.left-2,key.bottom-key.top-2,frame);label(dc,key,names[k],eye);}
    }else{
        RECT info={8,h-102,w-8,h-57};std::wstring text=wide(reply.empty()?(worker?"Thinking...":"Type a message and press Enter."):reply);
        SetTextColor(dc,eye);SetBkMode(dc,TRANSPARENT);DrawText(dc,text.c_str(),-1,&info,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
        RECT edit={8,h-55,w-8,h-31};std::wstring shown=wide("> "+(input.size()>50?input.substr(input.size()-50):input)+"_");
        DrawText(dc,shown.c_str(),-1,&edit,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
        typeButton={0,h-32,w/5,h};sendButton={w/5,h-32,w*2/5,h};soundButton={w*2/5,h-32,w*3/5,h};colorButton={w*3/5,h-32,w*4/5,h};exitButton={w*4/5,h-32,w,h};
        RECT buttons[]={typeButton,sendButton,soundButton,colorButton,exitButton};
        LPCTSTR names[]={TEXT("Type"),worker?TEXT("Stop"):TEXT("Send"),soundOn?TEXT("Sound ON"):TEXT("Sound OFF"),TEXT("Settings"),TEXT("Exit")};
        for(int i=0;i<5;i++){RECT r=buttons[i];box(dc,r.left+1,r.top+1,r.right-r.left-2,r.bottom-r.top-2,frame);label(dc,r,names[i],eye);}
    }
    if(bitmap){BitBlt(screen,0,0,w,h,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);}
    if(buffer)DeleteDC(buffer);
    EndPaint(hwnd,&ps);
}
static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    int x=(short)LOWORD(lp),y=(short)HIWORD(lp);
    switch(msg){
    case WM_CREATE:started=GetTickCount();lastActivity=started;touched=started-2000;gestureAt=started-2000;moodAt=started;SetTimer(hwnd,1,80,NULL);return 0;
    case WM_TIMER:
        if(wave&&(header.dwFlags&WHDR_DONE)){closeAudio();talking=false;}
        if(worker&&WaitForSingleObject(worker,0)==WAIT_OBJECT_0){
            CloseHandle(worker);worker=NULL;lastActivity=GetTickCount();moodAt=lastActivity;
            if(closing){DestroyWindow(hwnd);return 0;}
        }
        if(worker&&soundOn&&!replyTone&&!getResponse().empty()){replyTone=true;beep(mood);}
        if(!worker&&GetTickCount()-moodAt>6000)mood=0;
        if(!worker&&!pressed&&!keyboard&&!settings&&sleepSeconds&&GetTickCount()-lastActivity>(DWORD)sleepSeconds*1000)asleep=true;
        InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_SIZE:InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_PAINT:paint(hwnd);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONDOWN:
        lastActivity=GetTickCount();
        if(asleep){asleep=false;return 0;}
        if(settings){
            RECT r;GetClientRect(hwnd,&r);int rh=max2(1,(r.bottom-30)/7);
            if(y>=30)switch(clamp((y-30)/rh,0,6)){
                case 0:palette=(palette+1)%6;break;
                case 1:tracking=!tracking;break;
                case 2:scanlines=!scanlines;break;
                case 3:soundOn=!soundOn;if(!soundOn){closeAudio();talking=false;}break;
                case 4:sleepSeconds=sleepSeconds==45?90:sleepSeconds==90?0:sleepSeconds==0?15:45;break;
                case 5:settings=false;if(!worker)asleep=true;break;
                case 6:settings=false;break;
            }
            return 0;
        }
        if(y<26&&!keyboard){settings=true;return 0;}
        if(keyboard){
            RECT r;GetClientRect(hwnd,&r);int w=max2(1,r.right),kh=max2(1,(r.bottom-60)/5);
            if(y>=60){
                int row=(y-60)/kh;
                const char* rows[]={"1234567890","qwertyuiop","asdfghjkl","zxcvbnm,.?"};
                if(row<4){int n=(int)strlen(rows[row]),k=clamp(x*n/w,0,n-1);char ch=rows[row][k];if(shift&&ch>='a'&&ch<='z')ch-=32;if(input.size()<100)input+=ch;shift=false;}
                else switch(clamp(x*5/w,0,4)){
                    case 0:shift=!shift;break;
                    case 1:if(input.size()<100)input+=' ';break;
                    case 2:if(!input.empty())input.pop_back();break;
                    case 3:send();break;
                    case 4:keyboard=false;break;
                }
            }
            return 0;
        }
        if(in(exitButton,x,y)){requestClose(hwnd);return 0;}
        if(in(typeButton,x,y)){keyboard=true;return 0;}
        if(in(sendButton,x,y)){if(worker)InterlockedExchange(&cancelWork,1);else send();return 0;}
        if(in(soundButton,x,y)){soundOn=!soundOn;if(!soundOn){closeAudio();talking=false;}return 0;}
        if(in(colorButton,x,y)){settings=true;return 0;}
        pressed=true;stroking=false;gesture=0;pressedAt=GetTickCount();downX=pointerX=x;downY=pointerY=y;SetCapture(hwnd);return 0;
    case WM_MOUSEMOVE:
        if(pressed){pointerX=x;pointerY=y;lastActivity=GetTickCount();if(abs(x-downX)+abs(y-downY)>14)stroking=true;}
        return 0;
    case WM_LBUTTONUP:
        if(pressed){
            DWORD now=GetTickCount();bool pat=stroking||now-pressedAt>=350;
            pressed=false;lastActivity=gestureAt=now;
            if(pat){gesture=1;pokeCount=0;}
            else{pokeCount=now-lastPoke<1100?pokeCount+1:1;lastPoke=now;gesture=pokeCount>=3?3:2;}
            ReleaseCapture();
        }
        return 0;
    case WM_KEYUP:if(wp==VK_ESCAPE){if(settings)settings=false;else if(keyboard)keyboard=false;else requestClose(hwnd);return 0;}lastKey=(int)wp;return 0;
    case WM_CHAR:
        lastActivity=GetTickCount();asleep=false;
        if(settings)return 0;
        if(wp==13){send();return 0;}
        if(wp==8){if(!input.empty())input.pop_back();return 0;}
        if(wp>=32&&wp<127&&input.size()<100)input+=(char)wp;
        return 0;
    case WM_CLOSE:requestClose(hwnd);return 0;
    case WM_DESTROY:KillTimer(hwnd,1);closeAudio();PostQuitMessage(0);return 0;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    InitializeCriticalSection(&replyLock);
    GetModuleFileNameW(NULL,appDir,MAX_PATH);
    appDir[MAX_PATH-1]=0;WCHAR* slash=wcsrchr(appDir,L'\\');if(slash)slash[1]=0;else appDir[0]=0;
    WNDCLASS wc={};wc.hInstance=instance;wc.lpszClassName=TEXT("RelayCEHardware");wc.lpfnWndProc=proc;wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    if(!RegisterClass(&wc))return 1;
    HWND hwnd=CreateWindowEx(0,wc.lpszClassName,TEXT("RELAY CE"),WS_OVERLAPPED|WS_MAXIMIZE,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),NULL,NULL,instance,NULL);
    if(!hwnd)return 2;
    ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);
    MSG m={};while(GetMessage(&m,NULL,0,0)>0){TranslateMessage(&m);DispatchMessage(&m);}
    DeleteCriticalSection(&replyLock);return (int)m.wParam;
}
