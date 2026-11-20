# list windows of a pid / capture
import ctypes,ctypes.wintypes as W,sys
u=ctypes.windll.user32
def wins(pid=None,title=None):
    out=[]
    @ctypes.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    def cb(h,l):
        p=W.DWORD();u.GetWindowThreadProcessId(h,ctypes.byref(p))
        b=ctypes.create_unicode_buffer(256);u.GetWindowTextW(h,b,256)
        c=ctypes.create_unicode_buffer(256);u.GetClassNameW(h,c,256)
        r=W.RECT();u.GetWindowRect(h,ctypes.byref(r))
        if (pid is None or p.value==pid) and (title is None or b.value==title):
            out.append((h,p.value,b.value,c.value,(r.left,r.top,r.right,r.bottom),u.IsWindowVisible(h)))
        return True
    u.EnumWindows(cb,0);return out
if __name__=='__main__':
    for w in wins(int(sys.argv[1]) if len(sys.argv)>1 else None): print(w)
