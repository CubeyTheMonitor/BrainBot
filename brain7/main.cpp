#define UNICODE
#define _UNICODE
#include <windows.h>
#include <tchar.h>

static HWND editor=NULL;
static int W=480,H=320,app=0,hidden=0,wx=86,wy=29,ww=350,wh=220;
static bool menu=false,drag=false,alt=false;
static int dx,dy;
static RECT startR, taskR, menuR, winR, closeR,minR;
static RECT icons[3],items[5],bar[3];
static TCHAR note[4096]=TEXT("Welcome to Brain7!\r\n");
static TCHAR folder[MAX_PATH]=TEXT("\\");
static TCHAR names[8][MAX_PATH];
static bool dirs[8];
static int count=0;
static int mn(int a,int b){return a<b?a:b;}
static int mx(int a,int b){return a>b?a:b;}
static int bound(int n,int a,int b){return mx(a,mn(n,b));}
static RECT rect(int x,int y,int w,int h){RECT r={x,y,x+w,y+h};return r;}
static bool hit(RECT r,int x,int y){return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;}
static void fill(HDC dc,RECT r,COLORREF c){HBRUSH b=CreateSolidBrush(c);if(b){FillRect(dc,&r,b);DeleteObject(b);}}
static void frame(HDC dc,RECT r,COLORREF c){fill(dc,rect(r.left,r.top,r.right-r.left,1),c);fill(dc,rect(r.left,r.bottom-1,r.right-r.left,1),c);fill(dc,rect(r.left,r.top,1,r.bottom-r.top),c);fill(dc,rect(r.right-1,r.top,1,r.bottom-r.top),c);}
static void text(HDC dc,RECT r,LPCTSTR s,COLORREF c,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE){SetBkMode(dc,TRANSPARENT);SetTextColor(dc,c);DrawText(dc,s,-1,&r,flags|DT_NOPREFIX);}
static void grad(HDC dc,RECT r,COLORREF a,COLORREF b){int h=mx(1,r.bottom-r.top);for(int i=0;i<h;i+=2){int k=i;COLORREF c=RGB((GetRValue(a)*(h-k)+GetRValue(b)*k)/h,(GetGValue(a)*(h-k)+GetGValue(b)*k)/h,(GetBValue(a)*(h-k)+GetBValue(b)*k)/h);fill(dc,rect(r.left,r.top+i,r.right-r.left,mn(2,h-i)),c);}}
static void button(HDC dc,RECT r,LPCTSTR s){grad(dc,r,RGB(255,255,255),RGB(193,216,235));frame(dc,r,RGB(100,135,164));text(dc,r,s,RGB(20,40,60),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
static LPCTSTR titleOf(int n){switch(n){case 1:return TEXT("Computer");case 2:return TEXT("Notepad");case 3:return TEXT("Personalization");default:return TEXT("About Brain7");}}
static void symbol(HDC dc,int n,int x,int y){
 if(n==1){fill(dc,rect(x,y,28,21),RGB(44,65,89));fill(dc,rect(x+2,y+2,24,17),RGB(110,191,234));fill(dc,rect(x+11,y+21,6,5),RGB(215,224,228));fill(dc,rect(x+5,y+26,18,2),RGB(215,224,228));}
 else if(n==2){fill(dc,rect(x+3,y,22,28),RGB(248,248,243));frame(dc,rect(x+3,y,22,28),RGB(120,150,166));for(int j=6;j<25;j+=5)fill(dc,rect(x+7,y+j,14,1),RGB(86,139,193));}
 else {fill(dc,rect(x,y,28,23),RGB(225,238,249));frame(dc,rect(x,y,28,23),RGB(110,150,182));fill(dc,rect(x+3,y+3,22,17),RGB(38,127,184));fill(dc,rect(x+3,y+13,22,7),RGB(77,174,119));}
}
static void layout(){
 ww=mn(350,W-12);wh=mn(230,H-54);wx=bound(wx,2,mx(2,W-ww-2));wy=bound(wy,2,mx(2,H-36-wh));
 winR=rect(wx,wy,ww,wh);closeR=rect(wx+ww-31,wy+3,26,20);minR=rect(wx+ww-57,wy+3,24,20);
 startR=rect(3,H-33,64,30);taskR=rect(72,H-32,125,28);menuR=rect(2,H-224,220,189);
 for(int i=0;i<3;i++)icons[i]=rect(4,6+i*64,80,61);
 for(int i=0;i<5;i++)items[i]=rect(menuR.left+7,menuR.top+28+i*30,menuR.right-menuR.left-14,27);
 for(int i=0;i<3;i++)bar[i]=rect(wx+7+i*72,wy+29,66,24);
}
static void scan(){
 count=0;TCHAR mask[MAX_PATH];if(lstrlen(folder)>MAX_PATH-4)return;
 lstrcpy(mask,folder);int n=lstrlen(mask);if(n&&mask[n-1]!=TEXT('\\'))lstrcat(mask,TEXT("\\"));lstrcat(mask,TEXT("*"));
 WIN32_FIND_DATA data;HANDLE f=FindFirstFile(mask,&data);if(f==INVALID_HANDLE_VALUE)return;
 do{if(lstrcmp(data.cFileName,TEXT("."))&&lstrcmp(data.cFileName,TEXT(".."))){lstrcpyn(names[count],data.cFileName,MAX_PATH);dirs[count]=(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;count++;}}while(count<8&&FindNextFile(f,&data));FindClose(f);
}
static void positionEdit(){
 if(!editor)return;
 if(app==2&&!menu){MoveWindow(editor,wx+7,wy+58,ww-14,wh-65,TRUE);ShowWindow(editor,SW_SHOW);}
 else ShowWindow(editor,SW_HIDE);
}
static void snapshot(){if(editor)GetWindowText(editor,note,4096);}
static void openApp(HWND h,int n){snapshot();app=n;hidden=0;menu=false;layout();if(n==1)scan();positionEdit();InvalidateRect(h,NULL,FALSE);}
static void saveNote(HWND h){
 snapshot();TCHAR path[MAX_PATH];DWORD n=GetModuleFileName(NULL,path,MAX_PATH);
 if(!n||n>=MAX_PATH)return;int i=lstrlen(path)-1;while(i>=0&&path[i]!=TEXT('\\'))i--;path[i+1]=0;
 if(lstrlen(path)>MAX_PATH-20)return;lstrcat(path,TEXT("Brain7-note.txt"));
 HANDLE f=CreateFile(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
 if(f==INVALID_HANDLE_VALUE){MessageBox(h,TEXT("Could not save note."),TEXT("Notepad"),MB_OK);return;}
 DWORD wrote=0;const WCHAR bom=0xFEFF;DWORD size=lstrlen(note)*sizeof(TCHAR);BOOL ok=WriteFile(f,&bom,sizeof(bom),&wrote,NULL)&&wrote==sizeof(bom);
 if(ok)ok=WriteFile(f,note,size,&wrote,NULL)&&wrote==size;CloseHandle(f);
 MessageBox(h,ok?TEXT("Saved Brain7-note.txt beside AppMain.exe."):TEXT("Save failed."),TEXT("Notepad"),MB_OK);
}
static void draw(HWND h){
 PAINTSTRUCT ps;HDC screen=BeginPaint(h,&ps);HDC mem=CreateCompatibleDC(screen);HBITMAP bmp=mem?CreateCompatibleBitmap(screen,W,H):NULL;HGDIOBJ old=bmp?SelectObject(mem,bmp):NULL;HDC dc=bmp?mem:screen;
 grad(dc,rect(0,0,W,H),alt?RGB(34,61,86):RGB(16,76,133),alt?RGB(74,123,112):RGB(71,175,222));
 // Original geometric wallpaper, not copied Windows assets.
 for(int i=0;i<5;i++)fill(dc,rect(W-25-i*12,0,2,H-35),alt?RGB(79,130,125):RGB(87,180,221));
 LPCTSTR labels[]={TEXT("Computer"),TEXT("Notepad"),TEXT("Personalize")};
 for(int i=0;i<3;i++){symbol(dc,i+1,30,icons[i].top+5);text(dc,rect(4,icons[i].top+34,80,20),labels[i],RGB(255,255,255),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
 if(app){
 fill(dc,rect(wx+3,wy+3,ww,wh),RGB(37,83,121));fill(dc,winR,RGB(228,239,248));frame(dc,winR,RGB(70,105,138));grad(dc,rect(wx+1,wy+1,ww-2,25),RGB(202,227,248),RGB(125,171,210));
 text(dc,rect(wx+8,wy+2,ww-72,22),titleOf(app),RGB(20,43,68));button(dc,minR,TEXT("_"));grad(dc,closeR,RGB(235,145,136),RGB(170,51,48));frame(dc,closeR,RGB(150,65,62));text(dc,closeR,TEXT("X"),RGB(255,255,255),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
 if(app==1){button(dc,bar[0],TEXT("Up"));button(dc,bar[1],TEXT("Refresh"));text(dc,rect(wx+7,wy+56,ww-14,19),folder,RGB(30,55,75));fill(dc,rect(wx+7,wy+77,ww-14,wh-84),RGB(255,255,255));for(int i=0;i<count&&wy+79+i*17<wy+wh-17;i++){text(dc,rect(wx+12,wy+79+i*17,28,17),dirs[i]?TEXT("[+]"):TEXT(" - "),RGB(70,120,155));text(dc,rect(wx+40,wy+79+i*17,ww-49,17),names[i],RGB(24,44,59));}}
 else if(app==2){button(dc,bar[0],TEXT("Save"));button(dc,bar[1],TEXT("New"));}
 else if(app==3){text(dc,rect(wx+10,wy+36,ww-20,22),TEXT("Desktop wallpaper"),RGB(29,55,78));button(dc,rect(wx+12,wy+68,125,42),TEXT("Ocean blue"));button(dc,rect(wx+148,wy+68,125,42),TEXT("Slate green"));text(dc,rect(wx+12,wy+130,ww-24,23),TEXT("Brain7 Basic - lightweight GDI"),RGB(43,66,87));}
 else {text(dc,rect(wx+12,wy+40,ww-24,24),TEXT("Brain7 Basic 0.1"),RGB(23,49,79));text(dc,rect(wx+12,wy+72,ww-24,24),TEXT("Native Windows CE application"),RGB(43,66,87));text(dc,rect(wx+12,wy+100,ww-24,24),TEXT("Sharp Brain ARM926 / GDI"),RGB(43,66,87));}
 }
 grad(dc,rect(0,H-35,W,35),RGB(69,98,128),RGB(25,47,73));frame(dc,rect(0,H-35,W,35),RGB(124,163,189));grad(dc,startR,RGB(77,158,112),RGB(29,102,67));frame(dc,startR,RGB(113,192,133));text(dc,startR,TEXT("Start"),RGB(255,255,255),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
 if(app||hidden)button(dc,taskR,titleOf(app?app:hidden));
 SYSTEMTIME st;GetLocalTime(&st);TCHAR clock[24];wsprintf(clock,TEXT("%02d:%02d"),st.wHour,st.wMinute);text(dc,rect(W-69,H-33,65,29),clock,RGB(255,255,255),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
 if(menu){grad(dc,menuR,RGB(225,239,250),RGB(164,196,221));frame(dc,menuR,RGB(61,98,129));text(dc,rect(12,menuR.top+3,200,23),TEXT("Brain7"),RGB(23,53,80));LPCTSTR m[]={TEXT("Computer"),TEXT("Notepad"),TEXT("Personalization"),TEXT("About Brain7"),TEXT("Exit to Windows CE")};for(int i=0;i<5;i++){fill(dc,items[i],RGB(245,249,252));text(dc,rect(items[i].left+8,items[i].top,195,27),m[i],RGB(24,48,70));}}
 if(bmp){BitBlt(screen,0,0,W,H,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bmp);}if(mem)DeleteDC(mem);EndPaint(h,&ps);
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM p,LPARAM l){
 int x=(short)LOWORD(l),y=(short)HIWORD(l);
 switch(m){
 case WM_CREATE:editor=CreateWindow(TEXT("EDIT"),note,WS_CHILD|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL,0,0,1,1,h,(HMENU)1,GetModuleHandle(NULL),NULL);if(editor)SendMessage(editor,EM_LIMITTEXT,4095,0);SetTimer(h,1,1000,NULL);return 0;
 case WM_SIZE:W=LOWORD(l);H=HIWORD(l);layout();positionEdit();InvalidateRect(h,NULL,FALSE);return 0;
 case WM_TIMER:InvalidateRect(h,NULL,FALSE);return 0;
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:draw(h);return 0;
 case WM_LBUTTONDOWN:
 if(hit(startR,x,y)){menu=!menu;positionEdit();InvalidateRect(h,NULL,FALSE);return 0;}
 if(menu){for(int i=0;i<5;i++)if(hit(items[i],x,y)){if(i==4){DestroyWindow(h);return 0;}openApp(h,i+1);return 0;}menu=false;positionEdit();InvalidateRect(h,NULL,FALSE);return 0;}
 if((app||hidden)&&hit(taskR,x,y)){snapshot();if(app){hidden=app;app=0;}else{app=hidden;hidden=0;}positionEdit();InvalidateRect(h,NULL,FALSE);return 0;}
 if(app&&hit(winR,x,y)){
 if(hit(closeR,x,y)||hit(minR,x,y)){snapshot();hidden=hit(minR,x,y)?app:0;app=0;positionEdit();InvalidateRect(h,NULL,FALSE);return 0;}
 if(y<wy+26){drag=true;dx=x-wx;dy=y-wy;SetCapture(h);return 0;}
 if(app==2){if(hit(bar[0],x,y))saveNote(h);else if(hit(bar[1],x,y)&&editor){if(MessageBox(h,TEXT("Clear current note?"),TEXT("Notepad"),MB_YESNO)==IDYES)SetWindowText(editor,TEXT(""));}}
 if(app==1){if(hit(bar[0],x,y)){int n=lstrlen(folder);while(n>1&&folder[n-1]==TEXT('\\'))folder[--n]=0;while(n>1&&folder[n-1]!=TEXT('\\'))folder[--n]=0;scan();}else if(hit(bar[1],x,y))scan();else if(y>=wy+79){int i=(y-wy-79)/17;if(i<count&&dirs[i]&&lstrlen(folder)+lstrlen(names[i])+2<MAX_PATH){if(folder[lstrlen(folder)-1]!=TEXT('\\'))lstrcat(folder,TEXT("\\"));lstrcat(folder,names[i]);scan();}}}
 if(app==3){if(hit(rect(wx+12,wy+68,125,42),x,y))alt=false;if(hit(rect(wx+148,wy+68,125,42),x,y))alt=true;}
 InvalidateRect(h,NULL,FALSE);return 0;}
 for(int i=0;i<3;i++)if(hit(icons[i],x,y)){openApp(h,i+1);return 0;}return 0;
 case WM_MOUSEMOVE:if(drag){wx=x-dx;wy=y-dy;layout();positionEdit();InvalidateRect(h,NULL,FALSE);}return 0;
 case WM_LBUTTONUP:if(drag){drag=false;ReleaseCapture();}return 0;
 case WM_DESTROY:KillTimer(h,1);PostQuitMessage(0);return 0;
 }
 return DefWindowProc(h,m,p,l);
}
int WINAPI WinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int){
 WNDCLASS wc={};wc.lpfnWndProc=proc;wc.hInstance=inst;wc.lpszClassName=TEXT("Brain7Desktop");if(!RegisterClass(&wc))return 1;
 HWND h=CreateWindow(wc.lpszClassName,TEXT("Brain7 Basic"),WS_POPUP|WS_VISIBLE,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),NULL,NULL,inst,NULL);if(!h)return 2;
 ShowWindow(h,SW_SHOW);UpdateWindow(h);MSG msg;while(GetMessage(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessage(&msg);}return 0;
}
