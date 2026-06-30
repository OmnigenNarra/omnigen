#include "stdafx.h"
#include "PlatformMisc.h"

bool isKeyDown(int key)
{
    return GetKeyState(key) < 0;
}
