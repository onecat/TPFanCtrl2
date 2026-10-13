#pragma once

#include "systemtraysdk.h"
#include "DynamicIcon.h"
#include <crtdbg.h>
#include <string>
#include <cstring>


class CTaskbarTextIcon
{
	private:

		CDynamicIcon *m_pDicon;
		int iFarbeIconC;
		int iFontIconC;
        LPCTSTR aTooltipC;
        // Cached visual state avoids allocating a fresh HICON and notifying
        // Explorer for every 500 ms title timer tick when nothing changed.
        int m_lastColor = -1;
        int m_lastFont = -1;
        std::string m_lastTooltip;

		void _createdicon(const int iFarbeIconC, LPCTSTR aTooltipC, const int iFontIconC) 
		{	
			if (m_pSystray) m_pSystray->SetTooltipText(aTooltipC);
			CDynamicIcon* pDiconTemp = new CDynamicIcon(m_line1, m_line2, iFarbeIconC, iFontIconC);
			if (m_pSystray) m_pSystray->SetIcon(pDiconTemp->GetHIcon());
			if (m_pDicon)delete m_pDicon;
			m_pDicon = pDiconTemp;
		};

		void _ballondicon(LPCTSTR szText,LPCTSTR szTitle,DWORD dwIcon,UINT uTimeout) 
		{
			if(m_pDicon) m_pSystray->ShowBalloon
				(
					szText, 
					szTitle, 
					dwIcon, 
					uTimeout
				);
		};

		void _createsystray
		(
			HINSTANCE hInst,
			HWND hWnd,
			UINT uCallbackMessage,
			UINT uID,
			LPCTSTR aTooltipC
		 ) 
		{
			if (m_pSystray) delete m_pSystray;
			m_pSystray = new CSystemTray();
			m_pSystray->Create
				(
					hInst,
					hWnd, 
					uCallbackMessage,
					aTooltipC,   // tooltip
					m_pDicon->GetHIcon(),
					0
				);  
		};


	protected:

		static const int LINESLEN = 4;
		CSystemTray *m_pSystray;
		char m_line1[LINESLEN],m_line2[LINESLEN];
		//TODO: implement set/get/use
		COLORREF m_TextColor, m_BgColor;
		bool m_BgTransparent;

		void _setlines(const char* line1,const char* line2) 
		{
			strncpy_s(m_line1,sizeof(m_line1),line1,3);
			strncpy_s(m_line2,sizeof(m_line2),line2,3);
			m_line1[LINESLEN-1] = 0;
			m_line2[LINESLEN-1] = 0;
		};

	public:

    CTaskbarTextIcon
	(
		HINSTANCE hInst,
		HWND hWnd,
		UINT uCallbackMessage,
		UINT uID,
		const char * line1 = 0, //can be 3 chars
		const char * line2 = 0, //can be 3 chars
		int iFarbeIconC = 0,
		int iFontIconC = 11,
		LPCTSTR aTooltipC = _T("")
    ) 
	: 
        m_pSystray(NULL),
        m_pDicon(NULL),
        m_TextColor(RGB(0,0,0)),
        m_BgColor(RGB(255,0,0)),
        m_BgTransparent(false)        
		{
			_setlines(line1,line2);
			_createdicon(iFarbeIconC, aTooltipC, iFontIconC);
			_createsystray(hInst, hWnd, uCallbackMessage, uID, aTooltipC);        
		};

    ~CTaskbarTextIcon(void)
	{
        if (m_pSystray)delete m_pSystray;
        if (m_pDicon)delete m_pDicon;
	};

    void ChangeText
	(
		const char * line1, 
		const char * line2 = 0, 
		const int iFarbeIconC = 0,
		const int iFontIconC = 9,
		LPCTSTR aTooltipC =_T("/0")
	) 
    {
        const char* first = line1 ? line1 : "";
        const char* second = line2 ? line2 : "";
        const char* tip = aTooltipC ? aTooltipC : "";

        // _setlines truncates both labels to three bytes, so compare the
        // rendered text, not the full (possibly longer) sensor name.
        char next1[LINESLEN] = {};
        char next2[LINESLEN] = {};
        strncpy_s(next1, sizeof(next1), first, LINESLEN-1);
        strncpy_s(next2, sizeof(next2), second, LINESLEN-1);
        const bool pixelsChanged =
            strcmp(m_line1, next1) != 0 || strcmp(m_line2, next2) != 0 ||
            m_lastColor != iFarbeIconC || m_lastFont != iFontIconC;
        const bool tooltipChanged = m_lastTooltip != tip;
        if (!pixelsChanged && !tooltipChanged)
            return;

        if (pixelsChanged) {
            _setlines(first, second);
            _createdicon(iFarbeIconC, tip, iFontIconC);
            m_lastColor = iFarbeIconC;
            m_lastFont = iFontIconC;
        } else if (m_pSystray) {
            // Tooltip changed but temperature bitmap did not.
            m_pSystray->SetTooltipText(tip);
        }
        m_lastTooltip = tip;
    }

    void DiShowballon
	(
		LPCTSTR szText    ,
		LPCTSTR szTitle   ,
		DWORD dwIcon      ,
		UINT uTimeout
	) 
	{
		_ballondicon( szText, szTitle, dwIcon, uTimeout); 
	}
};
