#ifndef FONTRELEASEOBSERVER_H
#define FONTRELEASEOBSERVER_H

#include "VxMath.h"

void CkreFontTestReset();
void CkreFontTestExpectReleased(FONT_HANDLE font);
void CkreFontTestDeleteFont(FONT_HANDLE font);

#endif // FONTRELEASEOBSERVER_H
