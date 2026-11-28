from pathlib import Path
import re

p = Path("src/OSGLUWIN.c")
s = p.read_text()
def replace_once(old,new):
    global s
    if s.count(old)!=1:
        raise RuntimeError("Unexpected source: "+old[:100])
    s=s.replace(old,new,1)

# Keep the Sharp's existing display orientation and hardware button mappings.
for name in ("Init_ChangeOrientation", "Uninit_ChangeOrientation", "InitHotKeys", "UninitHotKeys"):
    pat = r"(LOCAL(?:FUNC blnr|PROC) "+name+r"\(void\)\s*\{).*?(\n\})"
    body = "\n\treturn trueblnr;" if name.startswith("Init") else ""
    s,n=re.subn(pat,lambda m:m[1]+body+m[2],s,flags=re.S)
    assert n==1, name

# Remove Pocket PC soft keyboard handling; physical keys are ordinary Mac keys.
for key,anchor in (("WM_SYSKEYDOWN","if (! TestBit(lparam, 30))"),("WM_SYSKEYUP","DoVKcode(wparam, lparam >> 24, falseblnr);")):
    start=s.index("case "+key+":",s.index("LOCALVAR blnr SipOn"))
    a=s.index("#if UseWinCE",start)
    b=s.index("#endif",a)+len("#endif")
    assert anchor in s[b:b+160]
    s=s[:a]+s[b:]
s=re.sub(r"(?m)^\s*SHFullScreen\(MainWnd, SHFS_[A-Z]+\);", "",s)
s=s.replace("timeGetTime()", "GetTickCount()")

# Small always-visible exit control in the physical bottom-right corner.
helper = '''
LOCALPROC BrainDrawExit(void)
{
    RECT r;
    HDC dc;
    if (MainWnd == NULL) return;
    if (!GetClientRect(MainWnd, &r)) return;
    r.left = r.right - 43; r.top = r.bottom - 22;
    dc = GetDC(MainWnd);
    if (dc == NULL) return;
    FillRect(dc, &r, GetStockObject(WHITE_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0,0,0));
    DrawText(dc, TEXT("EXIT"), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    ReleaseDC(MainWnd, dc);
}
LOCALFUNC blnr BrainExitHit(HWND h, LPARAM lp)
{
    RECT r;
    if (!GetClientRect(h, &r)) return falseblnr;
    return ((short)LOWORD(lp) >= r.right-43 &&
            (short)HIWORD(lp) >= r.bottom-22);
}
LOCALPROC BrainConfirmExit(HWND h)
{
    if (MessageBox(h, TEXT("Quit Mini vMac? Save your work first."),
        TEXT("Mini vMac"), MB_YESNO | MB_ICONQUESTION) == IDYES)
        ForceMacOff = trueblnr;
}
'''
replace_once("LOCALPROC Screen_DrawAll(void)",helper+"\nLOCALPROC Screen_DrawAll(void)")
replace_once("HaveChangedScreenBuff(0, 0, vMacScreenHeight, vMacScreenWidth);","HaveChangedScreenBuff(0, 0, vMacScreenHeight, vMacScreenWidth);\n\tBrainDrawExit();")
replace_once("ScreenClearChanges();\n\t}\n}\n\nGLOBALOSGLUPROC DoneWithDrawingForTick", "ScreenClearChanges();\n\t\tBrainDrawExit();\n\t}\n}\n\nGLOBALOSGLUPROC DoneWithDrawingForTick")
# Only modify the main window handler, not the upstream modal/control window.
pos=s.index("LOCALVAR blnr SipOn")
head,tail=s[:pos],s[pos:]
tail=tail.replace("case WM_SYSKEYDOWN:", "case WM_SYSKEYDOWN:\n\t\t\tif (wparam == VK_F12) { BrainConfirmExit(hwnd); return 0; }",1)
tail=tail.replace("case WM_RBUTTONDOWN:\n", "case WM_RBUTTONDOWN:\n\t\t\tif (BrainExitHit(hwnd, lparam)) { BrainConfirmExit(hwnd); return 0; }\n",1)
s=head+tail
p.write_text(s)

# The Sharp uses native CE, not Windows Mobile's aygshell.
p=Path("setup/WRCNFGAP.i")
s=p.read_text()
s=re.sub(r'(?m)^.*WriteDestFileLn\("#include <aygshell.h>"\);.*\n','',s)
p.write_text(s)
