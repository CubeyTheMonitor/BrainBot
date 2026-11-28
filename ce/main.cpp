// RELAY CE hardware proof of concept. No inference engine yet.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <mmsystem.h>
#include <tchar.h>
static DWORD started, touched, pressedAt;
static bool pressed=false;
static int pointerX=-1, pointerY=-1, lastKey=0, palette=0;
static bool talking=false;
static HWAVEOUT wave=NULL;
static WAVEHDR header={};
static short samples[4000];
static LPCTSTR audioStatus=TEXT("Sound not tested");
static RECT soundButton, colorButton, exitButton;
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
static void beep(){
    if(wave)return;
    WAVEFORMATEX fmt={};fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=1;
    fmt.nSamplesPerSec=16000;fmt.wBitsPerSample=16;fmt.nBlockAlign=2;fmt.nAvgBytesPerSec=32000;
    if(waveOutOpen(&wave,WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){
        wave=NULL;audioStatus=TEXT("Audio open failed");return;
    }
    for(int i=0;i<4000;i++){
        int period=i<2000?32:23;
        int amp=min2(1800,min2(i*20,(3999-i)*20));
        samples[i]=short((i%period<period/2)?amp:-amp);
    }
    ZeroMemory(&header,sizeof(header));header.lpData=(LPSTR)samples;header.dwBufferLength=sizeof(samples);
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
    COLORREF bg=palette?RGB(41,32,8):RGB(8,20,10);
    COLORREF frame=palette?RGB(120,96,37):RGB(53,80,57);
    COLORREF eye=palette?RGB(232,185,77):RGB(129,201,107);
    box(dc,0,0,w,h,bg);
    DWORD now=GetTickCount(),elapsed=now-started;
    bool happy=(pressed&&now-pressedAt>250)||(now-touched<1000);
    int unit=max2(1,min2(w/12,h/8)),cy=h/2-8;
    int eh=happy?unit:unit*2;
    if(elapsed%5000>4800)eh=max2(2,unit/4);
    for(int i=0;i<2;i++){
        int x=w*(i?3:1)/4, y=cy-unit/2;
        box(dc,x-unit*3/2,y-unit*3/2,unit*3,unit*3,frame);
        box(dc,x-unit*3/2+3,y-unit*3/2+3,unit*3-6,unit*3-6,bg);
        int dx=pointerX<0?0:clamp((pointerX-x)/12,-unit/3,unit/3);
        int dy=pointerY<0?0:clamp((pointerY-y)/16,-unit/5,unit/5);
        box(dc,x-unit*3/4+dx,y-eh/2+dy,unit*3/2,eh,eye);
    }
    int my=cy+unit*3/2,th=max2(2,unit/9);
    if(talking){
        for(int i=0;i<9;i++){
            int bar=th+int((elapsed/70+i*3)%5)*th;
            box(dc,w/2-unit*3/2+i*unit/3,my-bar/2,max2(2,unit/4),bar,eye);
        }
    }else{
        box(dc,w/2-unit,my,unit*2,th,eye);
        if(happy){box(dc,w/2-unit,my-unit/3,th,unit/3,eye);box(dc,w/2+unit-th,my-unit/3,th,unit/3,eye);}
    }
    RECT title={0,2,w,25};label(dc,title,TEXT("RELAY CE / HARDWARE TEST"),eye);
    TCHAR status[100];wsprintf(status,TEXT("%dx%d  Key: %d  %s"),w,h,lastKey,audioStatus);
    RECT info={0,h-59,w,h-34};label(dc,info,status,eye);
    soundButton={0,h-32,w/3,h};colorButton={w/3,h-32,w*2/3,h};exitButton={w*2/3,h-32,w,h};
    RECT buttons[]={soundButton,colorButton,exitButton};
    LPCTSTR names[]={TEXT("Beep"),TEXT("Palette"),TEXT("Exit")};
    for(int i=0;i<3;i++){RECT r=buttons[i];box(dc,r.left+1,r.top+1,r.right-r.left-2,r.bottom-r.top-2,frame);label(dc,r,names[i],eye);}
    if(bitmap){BitBlt(screen,0,0,w,h,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);}
    if(buffer)DeleteDC(buffer);
    EndPaint(hwnd,&ps);
}
static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    int x=(short)LOWORD(lp),y=(short)HIWORD(lp);
    switch(msg){
    case WM_CREATE:started=GetTickCount();touched=started-2000;SetTimer(hwnd,1,80,NULL);return 0;
    case WM_TIMER:
        if(wave&&(header.dwFlags&WHDR_DONE)){closeAudio();audioStatus=TEXT("Beep completed");talking=false;}
        InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_SIZE:InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_PAINT:paint(hwnd);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONDOWN:
        if(in(exitButton,x,y)){DestroyWindow(hwnd);return 0;}
        if(in(soundButton,x,y)){beep();return 0;}
        if(in(colorButton,x,y)){palette=!palette;return 0;}
        pressed=true;pressedAt=GetTickCount();pointerX=x;pointerY=y;SetCapture(hwnd);return 0;
    case WM_MOUSEMOVE:if(pressed){pointerX=x;pointerY=y;}return 0;
    case WM_LBUTTONUP:if(pressed){pressed=false;touched=GetTickCount();ReleaseCapture();}return 0;
    case WM_KEYUP:if(wp==VK_ESCAPE){DestroyWindow(hwnd);return 0;}lastKey=(int)wp;return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:KillTimer(hwnd,1);closeAudio();PostQuitMessage(0);return 0;
    }
    return DefWindowProc(hwnd,msg,wp,lp);
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    WNDCLASS wc={};wc.hInstance=instance;wc.lpszClassName=TEXT("RelayCEHardware");wc.lpfnWndProc=proc;wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    if(!RegisterClass(&wc))return 1;
    HWND hwnd=CreateWindowEx(0,wc.lpszClassName,TEXT("RELAY CE"),WS_OVERLAPPED|WS_MAXIMIZE,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),NULL,NULL,instance,NULL);
    if(!hwnd)return 2;
    ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);
    MSG m={};while(GetMessage(&m,NULL,0,0)>0){TranslateMessage(&m);DispatchMessage(&m);}
    return (int)m.wParam;
}
