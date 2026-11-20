# capture a window by exact title (and optional pid) with PrintWindow
import ctypes,ctypes.wintypes as W,sys,os
from PIL import Image
sys.path.insert(0,os.path.dirname(__file__))
from wl import wins
u=ctypes.windll.user32;g=ctypes.windll.gdi32
def cap(h,out,flag=2):
    r=W.RECT();u.GetClientRect(h,ctypes.byref(r));w,hh=r.right,r.bottom
    dc=u.GetDC(h);mdc=g.CreateCompatibleDC(dc);bm=g.CreateCompatibleBitmap(dc,w,hh);g.SelectObject(mdc,bm)
    ok=u.PrintWindow(h,mdc,flag|1)
    class BIH(ctypes.Structure):
        _fields_=[('a',W.DWORD),('w',W.LONG),('h',W.LONG),('p',W.WORD),('b',W.WORD),('c',W.DWORD),('s',W.DWORD),('x',W.LONG),('y',W.LONG),('u',W.DWORD),('i',W.DWORD)]
    bi=BIH(40,w,-hh,1,32,0,0,0,0,0,0);buf=ctypes.create_string_buffer(w*hh*4)
    g.GetDIBits(mdc,bm,0,hh,buf,ctypes.byref(bi),0)
    im=Image.frombuffer('RGBA',(w,hh),buf,'raw','BGRA',0,1).convert('RGB');im.save(out)
    g.DeleteObject(bm);g.DeleteDC(mdc);u.ReleaseDC(h,dc);return ok,im.size
if __name__=='__main__':
    title=sys.argv[1];out=sys.argv[2]
    ws=[w for w in wins(title=title) if w[5]]
    if not ws: raise SystemExit('no window '+title)
    print(cap(ws[0][0],out))
