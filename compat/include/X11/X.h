#ifndef VIBE_COMPAT_X11_X_H
#define VIBE_COMPAT_X11_X_H

#include <compat_defs.h>

typedef unsigned long XID;
typedef XID Window;
typedef XID Drawable;
typedef XID Pixmap;
typedef XID Cursor;
typedef XID Font;
typedef XID Colormap;
typedef XID Atom;
typedef unsigned long VisualID;
typedef unsigned long Time;
typedef unsigned long KeySym;
typedef int Bool;
typedef int Status;

#ifndef True
#define True 1
#endif

#ifndef False
#define False 0
#endif

#ifndef None
#define None 0L
#endif

#define ParentRelative 1L
#define CopyFromParent 0L

#define InputOutput 1
#define InputOnly 2

#define QueuedAlready 0
#define QueuedAfterReading 1
#define QueuedAfterFlush 2

#define KeyPress 2
#define KeyRelease 3
#define ButtonPress 4
#define ButtonRelease 5
#define MotionNotify 6
#define EnterNotify 7
#define LeaveNotify 8
#define FocusIn 9
#define FocusOut 10
#define Expose 12
#define DestroyNotify 17
#define UnmapNotify 18
#define MapNotify 19
#define ReparentNotify 21
#define ConfigureNotify 22
#define ClientMessage 33

#define NoEventMask 0L
#define KeyPressMask (1L << 0)
#define KeyReleaseMask (1L << 1)
#define ButtonPressMask (1L << 2)
#define ButtonReleaseMask (1L << 3)
#define EnterWindowMask (1L << 4)
#define LeaveWindowMask (1L << 5)
#define PointerMotionMask (1L << 6)
#define ExposureMask (1L << 15)
#define StructureNotifyMask (1L << 17)
#define FocusChangeMask (1L << 21)

#define CWBackPixmap (1L << 0)
#define CWBackPixel (1L << 1)
#define CWBorderPixel (1L << 3)
#define CWEventMask (1L << 11)
#define CWColormap (1L << 13)

#define GCFunction (1L << 0)
#define GCForeground (1L << 2)
#define GCBackground (1L << 3)
#define GCLineWidth (1L << 4)

#define CoordModeOrigin 0

#define LineSolid 0
#define CapButt 1
#define JoinMiter 0

#define ZPixmap 2

#endif
