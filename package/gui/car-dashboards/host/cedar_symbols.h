/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_CEDAR_SYMBOLS_H
#define HZ_CEDAR_SYMBOLS_H
#define HZ_CEDAR_API(X) \
 X(create,TPlayerCreate) X(destroy,TPlayerDestroy) X(stop,TPlayerStop) \
 X(display,TPlayerSetVideoDisplay) X(notify,TPlayerSetNotifyCallback) \
 X(hold,TPlayerSetHoldLastPicture) X(source,TPlayerSetDataSource) \
 X(prepare,TPlayerPrepare) X(rect,TPlayerSetDisplayRect) X(duration,TPlayerGetDuration) \
 X(seek,TPlayerSeekTo) X(start,TPlayerStart) X(pause,TPlayerPause) X(position,TPlayerGetCurrentPosition)
#endif
