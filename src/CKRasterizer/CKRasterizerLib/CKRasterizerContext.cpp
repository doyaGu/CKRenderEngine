#include "CKRasterizer.h"

CKRasterizerContext::CKRasterizerContext()
    : m_Driver(NULL),
      m_PosX(0),
      m_PosY(0),
      m_Width(0),
      m_Height(0),
      m_Bpp(0),
      m_ZBpp(0),
      m_StencilBpp(0),
      m_Fullscreen(FALSE),
      m_RefreshRate(0),
      m_Window(NULL)
{
}
