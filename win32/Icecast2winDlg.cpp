// Icecast2winDlg.cpp : implementation file
//

#include "stdafx.h"
#include "Icecast2win.h"
#include "Icecast2winDlg.h"
#include <process.h>
#include "ResizableDialog.h"

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdlib.h>

extern "C" {
#include "thread.h"
#include "avl.h"
#include "log.h"
#include "global.h"
#include "httpp.h"
#include "sock.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
}


#include <afxinet.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

CEdit	*g_accessControl;
CEdit	*g_errorControl;
CIcecast2winDlg	*g_mainDialog;
bool	g_tailAccess = false;
bool	g_tailError = false;
void CollectStats(stats_event_t *event);
CString gConfigurationSave;

#define MAXSTATSPERSOURCE 30
#define MAXSOURCES 1024

typedef struct tagElement {
	CString	name;
	CString	value;
} Element;

typedef struct tagElementAdditional {
	CString source;
	CString	name;
	CString	value;
} ElementAdditional;


typedef struct tagMainElement {
//	char	source[1024];
	CString	source;
	long	numStats;
	Element	stats[MAXSTATSPERSOURCE];
} MainElement;

typedef struct tagMainElementAdditional {
	long	numStats;
	ElementAdditional	stats[MAXSTATSPERSOURCE];
} MainElementAdditional;


MainElement gStats[MAXSOURCES];
MainElement gGlobalStats;
MainElementAdditional	gAdditionalGlobalStats;

long	numMainStats;

extern "C" {
	int main(int argc, char **argv);
}

void AddToAdditionalGlobalStats(CString source, CString name) {
	int foundit = 0;
	for (int i=0;i<gAdditionalGlobalStats.numStats;i++) {
		if ((gAdditionalGlobalStats.stats[i].source == source) && (gAdditionalGlobalStats.stats[i].name == name)) {
			foundit = 1;
			break;
		}
	}
	if (!foundit) {
		gAdditionalGlobalStats.stats[gAdditionalGlobalStats.numStats].source = source;
		gAdditionalGlobalStats.stats[gAdditionalGlobalStats.numStats].name = name;
		gAdditionalGlobalStats.numStats++;
	}
	g_mainDialog->UpdateStatsLists();
}
void RemoveFromAdditionalGlobalStats(CString source, CString name) {
	int foundit = 0;
	for (int i=0;i<gAdditionalGlobalStats.numStats;i++) {
		if ((gAdditionalGlobalStats.stats[i].source == source) && (gAdditionalGlobalStats.stats[i].name == name)) {
			for (int j=i+1;j < gAdditionalGlobalStats.numStats;j++) {
				gAdditionalGlobalStats.stats[j-1].name = gAdditionalGlobalStats.stats[j].name;
				gAdditionalGlobalStats.stats[j-1].value = gAdditionalGlobalStats.stats[j].value;
				gAdditionalGlobalStats.stats[j-1].source = gAdditionalGlobalStats.stats[j].source;
			}
			gAdditionalGlobalStats.numStats--;
			break;
		}
	}
	g_mainDialog->UpdateStatsLists();
}
/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

class CAboutDlg : public CResizableDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CResizableDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CResizableDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
		// No message handlers
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CIcecast2winDlg dialog

CIcecast2winDlg::CIcecast2winDlg(CWnd* pParent /*=NULL*/)
	: CResizableDialog(CIcecast2winDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CIcecast2winDlg)
	m_AccessEdit = _T("");
	m_ErrorEdit = _T("");
	m_ConfigEdit = _T("");
	m_ServerStatus = _T("");
	m_SourcesConnected = _T("");
	m_NumClients = _T("");
	m_StatsEdit = _T("");
	m_Autostart = FALSE;
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CIcecast2winDlg::DoDataExchange(CDataExchange* pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CIcecast2winDlg)
	DDX_Control(pDX, IDC_STATIC_SS, m_SS);
	DDX_Control(pDX, IDC_SERVERSTATUS, m_ServerStatusBitmap);
	DDX_Control(pDX, IDC_START, m_StartButton);
	DDX_Control(pDX, IDC_MAINTAB, m_MainTab);
	DDX_Check(pDX, IDC_AUTOSTART, m_Autostart);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CIcecast2winDlg, CResizableDialog)
	//{{AFX_MSG_MAP(CIcecast2winDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_NOTIFY(TCN_SELCHANGE, IDC_MAINTAB, OnSelchangeMaintab)
	ON_COMMAND(ID_FILE_EXIT, OnFileExit)
	ON_WM_TIMER()
	ON_COMMAND(ID_FILE_STARTSERVER, OnFileStartserver)
	ON_COMMAND(ID_FILE_STOPSERVER, OnFileStopserver)
	ON_BN_CLICKED(IDC_START, OnStart)
	ON_WM_CLOSE()
	ON_WM_SIZE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CIcecast2winDlg message handlers

#include "colors.h"

BOOL CIcecast2winDlg::OnInitDialog()
{
	CResizableDialog::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	
	g_mainDialog = this;
//	statsTab.SetDialogBkColor(BGCOLOR,TEXTCOLOR);

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	
	// TODO: Add extra initialization here

	config_read();

	statsTab.m_colSource0Width = m_colSource0Width;
	statsTab.m_colStats0Width = m_colStats0Width;
	statsTab.m_colStats1Width = m_colStats1Width;
	statusTab.m_colStats0Width = m_colGStats0Width;
	statusTab.m_colStats1Width = m_colGStats1Width;
	statusTab.m_colStats2Width = m_colGStats2Width;
	


	configTab.Create(IDD_CONFIGDIALOG, this);
	statsTab.Create(IDD_STATSDIALOG, this);
	statusTab.Create(IDD_SSTATUS, this);

	int nPageID = 0;
	m_MainTab.AddSSLPage (_T("Server Status"), nPageID, (CTabPageSSL *)&statusTab);
	nPageID++;
	m_MainTab.AddSSLPage (_T("Configuration"), nPageID, (CTabPageSSL *)&configTab);
	nPageID++;
	m_MainTab.AddSSLPage (_T("Stats"), nPageID, (CTabPageSSL *)&statsTab);
	nPageID++;

	
	labelFont.CreateFont(24,0, 0, 0, FW_BOLD, 0, 0, 0, 0, OUT_TT_PRECIS, 0, PROOF_QUALITY, 0, "Arial");
	
	runningBitmap.LoadBitmap(IDB_BITMAP6);
	stoppedBitmap.LoadBitmap(IDB_BITMAP5);
	m_SS.SetFont(&labelFont, TRUE);

	UpdateData(FALSE);

	LoadConfig();

	AddAnchor(IDC_MAINTAB, TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_STATICBLACK, TOP_LEFT, TOP_RIGHT);

	EnableSaveRestore("icecast2win", "positions");
	


	if (m_Autostart) {
		OnStart();
	}
	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CIcecast2winDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CResizableDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CIcecast2winDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CResizableDialog::OnPaint();
	}
}


// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CIcecast2winDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

void CIcecast2winDlg::OnSelchangeMaintab(NMHDR* pNMHDR, LRESULT* pResult) 
{
	// TODO: Add your control notification handler code here

	/*
	if (m_MainTab.GetCurSel() == 0) {
		EnableControl(IDC_NUMBER_CLIENTS);
		EnableControl(IDC_SERVERSTATUS);
		EnableControl(IDC_SOURCES_CONNECTED);
		m_ConfigEditCtrl.ShowWindow(SW_HIDE);
		m_StatsEditCtrl.ShowWindow(SW_HIDE);
	}
	if (m_MainTab.GetCurSel() == 99) {
		DisableControl(IDC_NUMBER_CLIENTS);
		DisableControl(IDC_SERVERSTATUS);
		DisableControl(IDC_SOURCES_CONNECTED);
		m_ConfigEditCtrl.ShowWindow(SW_HIDE);
		m_StatsEditCtrl.ShowWindow(SW_HIDE);
	}
	if (m_MainTab.GetCurSel() == 99) {
		DisableControl(IDC_NUMBER_CLIENTS);
		DisableControl(IDC_SERVERSTATUS);
		DisableControl(IDC_SOURCES_CONNECTED);
		m_ConfigEditCtrl.ShowWindow(SW_HIDE);
		m_StatsEditCtrl.ShowWindow(SW_HIDE);
	}
	if (m_MainTab.GetCurSel() == 1) {
		DisableControl(IDC_NUMBER_CLIENTS);
		DisableControl(IDC_SERVERSTATUS);
		DisableControl(IDC_SOURCES_CONNECTED);
		m_ConfigEditCtrl.ShowWindow(SW_SHOW);
		m_StatsEditCtrl.ShowWindow(SW_HIDE);
		LoadConfig();
		ParseConfig();
	}
	if (m_MainTab.GetCurSel() == 2) {
		DisableControl(IDC_NUMBER_CLIENTS);
		DisableControl(IDC_SERVERSTATUS);
		DisableControl(IDC_SOURCES_CONNECTED);
		m_ConfigEditCtrl.ShowWindow(SW_HIDE);
		m_StatsEditCtrl.ShowWindow(SW_SHOW);
		if (global.running == ICE_RUNNING) {
			CString	statData;
			for (int i=0;i<numMainStats;i++) {
				if (!strcmp(gStats[i].source, "")) {
					statData += "\r\nBase Server Data\r\n-------------------------\r\n";
					for (int j=0;j<gStats[i].numStats;j++) {
						statData += gStats[i].stats[j].name;
						statData += " = ";
						statData += gStats[i].stats[j].value;
						statData += "\r\n";
					}
				}
				else {
					statData += "\r\nData For Source ";
					statData += gStats[i].source;
					statData += "\r\n-------------------------\r\n";
					for (int j=0;j<gStats[i].numStats;j++) {
						statData += gStats[i].stats[j].name;
						statData += " = ";
						statData += gStats[i].stats[j].value;
						statData += "\r\n";
					}
				}
			}
			m_StatsEdit = statData;
			UpdateData(FALSE);
		}
		else {
			MessageBox("Server not running, cannot get stats", "Message", MB_OK);
		}

	}
*/
	*pResult = 0;
}

void CIcecast2winDlg::LoadConfig()
{
	FILE	*filep;
	char	buffer[2046] = "";
	CIcecast2winApp	*myApp = (CIcecast2winApp *)AfxGetApp();

	configTab.m_Config = "";
	filep = fopen(myApp->m_configFile, "r");
	if (filep) {
		while (!feof(filep)) { 
			memset(buffer, '\000', sizeof(buffer));
			fgets(buffer, sizeof(buffer), filep);
			if (strlen(buffer) > 0) {
				char *p1 = strstr(buffer, "\r\n");
				if (p1) {
					*p1 = '\000';
				}
				else {
					buffer[strlen(buffer)-1] = '\000';
				}
				configTab.m_Config = configTab.m_Config + buffer;
				configTab.m_Config += "\r\n";
			}
		}
	}
	else {
		configTab.m_Config = " \
<icecast>\r\n \
	<location>Here and There</location>\r\n\
	<admin>nobody@me.org</admin>\r\n\
	<limits>\r\n\
		<clients>100</clients>\r\n\
		<sources>2</sources>\r\n\
		<threadpool>5</threadpool>\r\n\
		<client-timeout>30</client-timeout>\r\n\
		<header-timeout>15</header-timeout>\r\n\
		<source-timeout>10</source-timeout>\r\n\
	</limits>\r\n\
	<source-password>changeme</source-password>\r\n\
	<directory>\r\n\
		<touch-freq>5</touch-freq>\r\n\
		<server>\r\n\
			<host>yp.icecast.org</host>\r\n\
			<touch-freq>15</touch-freq>\r\n\
		</server>\r\n\
	</directory>\r\n\
	<bind-address>0.0.0.0</bind-address>\r\n\
	<port>8000</port>\r\n\
        <paths>\r\n\
                <basedir>./</basedir>\r\n\
                <logdir>./</logdir>\r\n\
                <webroot>./webroot</webroot>\r\n\
        </paths>\r\n\
	<logging>\r\n\
		<accesslog>access.log</accesslog>\r\n\
		<errorlog>error.log</errorlog>\r\n\
	</logging>\r\n\
</icecast>\r\n\
";
	}
	gConfigurationSave = configTab.m_Config;
	configTab.UpdateData(FALSE);

}


void CIcecast2winDlg::OnFileExit() 
{
	// TODO: Add your command handler code here

	DestroyWindow();
}


void CIcecast2winDlg::ParseConfig()
{
	char	access[2046] = "";
	char	error[2046] = "";
	char	logdir[2046] = "";

	memset(access, '\000', sizeof(access));
	memset(error, '\000', sizeof(error));
	memset(logdir, '\000', sizeof(logdir));

	getTag(m_ConfigEdit.GetBuffer(0), "logdir", logdir);
	getTag(m_ConfigEdit.GetBuffer(0), "accesslog", access);
	getTag(m_ConfigEdit.GetBuffer(0), "errorlog", error);

	m_AccessLog = logdir;
	m_AccessLog += access;
	m_ErrorLog = logdir;
	m_ErrorLog += error;

}

void CIcecast2winDlg::getTag(char *pbuf, char *ptag, char *dest)
{
	char	openTag[256] = "";
	char	closeTag[256] = "";

	sprintf(openTag, "<%s>", ptag);
	sprintf(closeTag, "</%s>", ptag);
	
	char *p1;
	p1 = strstr(pbuf, openTag);
	if (p1) {
		p1 = p1 + strlen(openTag);
		char *p2;
		p2 = strstr(p1, closeTag);
		if (p2) {
			strncpy(dest, p1, p2-p1);
		}
	}
}


void CIcecast2winDlg::EnableControl(UINT control)
{
	CWnd	*pWnd;
	pWnd = GetDlgItem(control);
	::ShowWindow(pWnd->GetSafeHwnd(), SW_SHOW);
}

void CIcecast2winDlg::DisableControl(UINT control)
{
	CWnd	*pWnd;
	pWnd = GetDlgItem(control);
	::ShowWindow(pWnd->GetSafeHwnd(), SW_HIDE);
}



void CollectStats(stats_event_t *event)
{
	Element tempElement;
	char	tempSource[1024] = "";

	tempElement.name = "";
	tempElement.value = "";

//	memset(&tempElement, '\000', sizeof(tempElement));

	if (event->name != NULL) {
		//strcpy(tempElement.name, event->name);
		tempElement.name = event->name;
	}
	if (event->value != NULL) {
		//strcpy(tempElement.value, event->value);
		tempElement.value = event->value;
	}
	if (event->source != NULL) {
		strcpy(tempSource, event->source);
		
	}


	int foundit = 0;
	for (int i=0;i<numMainStats;i++) {
		if (!strcmp(gStats[i].source, tempSource)) {
			int foundit2 = 0;
			for (int j=0;j<gStats[i].numStats;j++) {
//				if (!strcmp(gStats[i].stats[j].name, tempElement.name)) {
//					strcpy(gStats[i].stats[j].value, tempElement.value);
				if (gStats[i].stats[j].name == tempElement.name) {
					//strcpy(gStats[i].stats[j].value, tempElement.value);
					gStats[i].stats[j].value = tempElement.value;

					foundit2 = 1;
				}
			}
			if (!foundit2) {
//				memcpy(&gStats[i].stats[gStats[i].numStats], &tempElement, sizeof(tempElement));
				gStats[i].stats[j].name = tempElement.name;
				gStats[i].stats[j].value = tempElement.value;
				gStats[i].numStats++;
			}
			foundit = 1;
		}
	}
	if (!foundit) {

//		strcpy(gStats[numMainStats].source, tempSource);
		gStats[numMainStats].source = tempSource;
		gStats[numMainStats].stats[0].name = tempElement.name;
		gStats[numMainStats].stats[0].value = tempElement.value;

//		memcpy(&gStats[numMainStats].stats[0], &tempElement, sizeof(tempElement));
		gStats[numMainStats].numStats++;
		numMainStats++;
	}
	g_mainDialog->UpdateStatsLists();

}
bool g_collectingStats = false;

void StartStats(void *dummy)
{
	while (global.running != ICE_RUNNING) {
		Sleep(500);
	}
	while (global.running == ICE_RUNNING) {
		if (global.running == ICE_RUNNING) {
//			memset(&gStats, '\000', sizeof(gStats));
			for (int j=0;j<MAXSOURCES;j++) {
				gStats[j].numStats = 0;
			}
			numMainStats = 0;
			stats_callback(CollectStats);
		}
		if (global.running != ICE_RUNNING) {
			_endthread();
		}
	}
	_endthread();
}
void CIcecast2winDlg::OnTimer(UINT nIDEvent) 
{
	// TODO: Add your message handler code here and/or call default
	if (nIDEvent == 0) {
		if (global.running == ICE_RUNNING) {
			char	buffer[255] = "";
			CString	tmp;
			// Get info from stats...
			m_ServerStatusBitmap.SetBitmap(HBITMAP(runningBitmap));
			sprintf(buffer, "%d", global.sources);
			tmp = buffer;
			if (tmp != statusTab.m_Sources) {
				statusTab.m_Sources = tmp;
				statusTab.UpdateData(FALSE);
			}
			sprintf(buffer, "%d", global.clients);
			tmp = buffer;
			if (tmp != statusTab.m_Clients) {
				statusTab.m_Clients = tmp;
				statusTab.UpdateData(FALSE);
			}

			m_StartButton.GetWindowText(tmp);

			if (tmp == "Start Server") {
				m_StartButton.SetWindowText("Stop Server");
				m_StartButton.SetState(0);
			}
			//UpdateData(FALSE);
			time_t	currentTime;
			time(&currentTime);
			time_t  runningTime = currentTime - serverStart;

			CTimeSpan runningFor(runningTime);

			char	timespan[1024] = "";
			sprintf(timespan, "%d Days, %d Hours, %d Minutes, %d Seconds", runningFor.GetDays(), runningFor.GetHours(), runningFor.GetMinutes(), runningFor.GetSeconds());
			statusTab.m_RunningFor = timespan;
			statusTab.UpdateData(FALSE);

			SetTimer(0, 500, NULL);
		}
		else {
			statusTab.m_Sources = "0";
			statusTab.m_Clients = "0";
			m_ServerStatusBitmap.SetBitmap(HBITMAP(stoppedBitmap));
			m_StartButton.SetWindowText("Start Server");
			m_StartButton.SetState(0);
			configTab.m_ConfigCtrl.SetReadOnly(FALSE);
			UpdateData(FALSE);
			statusTab.m_RunningFor = "Not running";
			statusTab.UpdateData(FALSE);
			//UpdateData(FALSE);

		}
	}
	
	CResizableDialog::OnTimer(nIDEvent);
}

char	g_configFile[1024] = "";
char	g_progName[255] = "icecast2";
//int		__argc = 2;
//char*	__argv[2];

void StartServer(void *configfile)
{
	int		argc = 3;
	char*	argv[3];

	strcpy(g_configFile, (char *)configfile);

	argv[0] = g_progName;
	argv[1] = "-c";
	argv[2] = g_configFile;
	time(&(g_mainDialog->serverStart));
	main(argc, (char **)argv);
//	g_mainDialog->StopServer();
	global.running = ICE_HALTING;
	_endthread();


}
void CIcecast2winDlg::OnFileStartserver() 
{
	// TODO: Add your command handler code here
	CIcecast2winApp	*myApp = (CIcecast2winApp *)AfxGetApp();

	if (gConfigurationSave == "") {
		gConfigurationSave = m_ConfigEdit;
	}

	if (global.running == ICE_RUNNING) {
		MessageBox("Server already running", "Error", MB_OK);
	}
	else {
		m_ConfigEditCtrl.SetReadOnly(TRUE);
		LoadConfig();
		ParseConfig();
		SetTimer(0, 500, NULL);
		_beginthread(StartServer, 0, (void *)(LPCSTR)myApp->m_configFile);
//		_beginthread(StartTailAccessLog, 0, (void *)0);
//		_beginthread(StartTailErrorLog, 0, (void *)0);
		// EDZ
		_beginthread(StartStats, 0, (void *)CollectStats);
	}
}

void CIcecast2winDlg::OnFileStopserver() 
{
	// TODO: Add your command handler code here
	;
}

bool infocus = false;

void CIcecast2winDlg::StopServer()
{
	KillTimer(0);
	configTab.m_ConfigCtrl.SetReadOnly(FALSE);
	global.running = ICE_HALTING;
	m_StartButton.SetWindowText("Start Server");
	m_StartButton.SetState(0);
	m_ServerStatusBitmap.SetBitmap(HBITMAP(stoppedBitmap));
	statusTab.m_RunningFor = "Not running";
	statusTab.UpdateData(FALSE);



}


void CIcecast2winDlg::OnStart() 
{
	CIcecast2winApp	*myApp = (CIcecast2winApp *)AfxGetApp();

	// TODO: Add your control notification handler code here
	if (global.running == ICE_RUNNING) {
		StopServer();
	}
	else {
		configTab.m_ConfigCtrl.SetReadOnly(TRUE);
		ParseConfig();
		SetTimer(0, 500, NULL);
		_beginthread(StartServer, 0, (void *)(LPCSTR)myApp->m_configFile);
		// EDZ
		_beginthread(StartStats, 0, (void *)CollectStats);
	}
	
}

void CIcecast2winDlg::UpdateStatsLists()
{
	char	item[1024] = "";
	int l = 0;

	for (int i=0;i<numMainStats;i++) {
		int inthere = 0;
		int k = 0;
		for (l=0;l < gAdditionalGlobalStats.numStats;l++) {
			for (int m=0;m < gStats[i].numStats;m++) {
				if ((gAdditionalGlobalStats.stats[l].source == gStats[i].source) &&
					(gAdditionalGlobalStats.stats[l].name == gStats[i].stats[m].name)) {
					gAdditionalGlobalStats.stats[l].value = gStats[i].stats[m].value;
					break;
				}
			}
		}
		if (strlen(gStats[i].source) > 0) {

			for (k=0;k < statsTab.m_SourceListCtrl.GetItemCount();k++) {

				statsTab.m_SourceListCtrl.GetItemText(k, 0, item, sizeof(item));
				if (!strcmp(gStats[i].source, item)) {
					inthere = 1;
					break;
				}
			}
			if (!inthere) {
				if (gStats[i].source != "") {
					LVITEM	lvi;

					lvi.mask =  LVIF_IMAGE | LVIF_TEXT;
					lvi.iItem = statsTab.m_SourceListCtrl.GetItemCount();
					lvi.iSubItem = 0;
					lvi.pszText = (LPTSTR)(LPCTSTR)gStats[i].source;
					statsTab.m_SourceListCtrl.InsertItem(&lvi);
				}
			}
			int nItemSelected = statsTab.m_SourceListCtrl.GetSelectionMark();
			if (nItemSelected != -1) {
				memset(item, '\000', sizeof(item));
				statsTab.m_SourceListCtrl.GetItemText(nItemSelected, 0, item, sizeof(item));
				if (!strcmp(gStats[i].source, item)) {
					for (int l=0;l<gStats[i].numStats;l++) {
						int inthere2 = 0;
						char	item2[1024] = "";
						for (int m=0;m < statsTab.m_StatsListCtrl.GetItemCount();m++) {
							statsTab.m_StatsListCtrl.GetItemText(m, 0, item2, sizeof(item2));
							if (!strcmp(gStats[i].stats[l].name, item2)) {
								LVITEM	lvi;

								lvi.mask =  LVIF_TEXT;
								lvi.iItem = m;
								lvi.iSubItem = 1;
								lvi.pszText = (LPTSTR)(LPCTSTR)gStats[i].stats[l].value;
								statsTab.m_StatsListCtrl.SetItem(&lvi);
								inthere2 = 1;
								break;
							}
						}
						if (!inthere2) {
							LVITEM	lvi;

							lvi.mask =  LVIF_TEXT;
							lvi.iItem = statsTab.m_StatsListCtrl.GetItemCount();
							lvi.iSubItem = 0;
							lvi.pszText = (LPTSTR)(LPCTSTR)gStats[i].stats[l].name;
							statsTab.m_StatsListCtrl.InsertItem(&lvi);
							lvi.iSubItem = 1;
							lvi.pszText = (LPTSTR)(LPCTSTR)gStats[i].stats[l].value;
							statsTab.m_StatsListCtrl.SetItem(&lvi);
						}
					}
				}
			}
			for (l=0;l < gAdditionalGlobalStats.numStats;l++) {
				int inthere2 = 0;
				char	item2[1024] = "";
				char	item3[1024] = "";
				CString	itemSource;
				CString itemName;
				for (int m=0;m < statusTab.m_GlobalStatList.GetItemCount();m++) {
					statusTab.m_GlobalStatList.GetItemText(m, 0, item2, sizeof(item2));
					statusTab.m_GlobalStatList.GetItemText(m, 1, item3, sizeof(item3));
					itemSource = item2;
					itemName = item3;
					if ((gAdditionalGlobalStats.stats[l].source == itemSource) &&
						(gAdditionalGlobalStats.stats[l].name == itemName)) {
						LVITEM	lvi;

						lvi.mask =  LVIF_TEXT;
						lvi.iItem = m;
						lvi.iSubItem = 2;
						lvi.pszText = (LPTSTR)(LPCTSTR)gAdditionalGlobalStats.stats[l].value;
						statusTab.m_GlobalStatList.SetItem(&lvi);
						inthere2 = 1;
						break;
					}
				}
				if (!inthere2) {
					LVITEM	lvi;

					lvi.mask =  LVIF_TEXT;
					lvi.iItem = statusTab.m_GlobalStatList.GetItemCount();
					lvi.iSubItem = 0;
					lvi.pszText = (LPTSTR)(LPCTSTR)gAdditionalGlobalStats.stats[l].source;
					statusTab.m_GlobalStatList.InsertItem(&lvi);
					lvi.iSubItem = 1;
					lvi.pszText = (LPTSTR)(LPCTSTR)gAdditionalGlobalStats.stats[l].name;
					statusTab.m_GlobalStatList.SetItem(&lvi);
					lvi.iSubItem = 2;
					lvi.pszText = (LPTSTR)(LPCTSTR)gAdditionalGlobalStats.stats[l].value;
					statusTab.m_GlobalStatList.SetItem(&lvi);
				}
			}
		}
		else {

			for (k=0;k < gStats[i].numStats;k++) {
				inthere = 0;
				for (l=0;l < statusTab.m_GlobalStatList.GetItemCount();l++) {

					statusTab.m_GlobalStatList.GetItemText(l, 1, item, sizeof(item));
					if (!strcmp(gStats[i].stats[k].name, item)) {
						inthere = 1;
						break;
					}
				}
				if (!inthere) {
					LVITEM	lvi;

					lvi.mask =  LVIF_IMAGE | LVIF_TEXT;
					lvi.iItem = statsTab.m_SourceListCtrl.GetItemCount();
					lvi.iSubItem = 0;
					lvi.pszText = "Global Stat";
					statusTab.m_GlobalStatList.InsertItem(&lvi);
					lvi.iSubItem = 1;
					lvi.pszText = (LPTSTR)(LPCTSTR)gStats[i].stats[k].name;
					statusTab.m_GlobalStatList.SetItem(&lvi);
					lvi.iSubItem = 2;
					lvi.pszText = (LPTSTR)(LPCTSTR)gStats[i].stats[k].value;
					statusTab.m_GlobalStatList.SetItem(&lvi);
				}
				else {
					LVITEM	lvi;

					lvi.mask =  LVIF_IMAGE | LVIF_TEXT;
					lvi.iItem = l;
					lvi.iSubItem = 2;
					lvi.pszText = (LPTSTR)(LPCTSTR)gStats[i].stats[k].value;
					statusTab.m_GlobalStatList.SetItem(&lvi);
				}
			}
		}
	}
}

char	gAppName[255] = "icecast2";
char	gConfigFile[255] = "icecast2.ini";

void CIcecast2winDlg::config_write()
{
	char	buf[255] = "";
	char	buf2[1024] = "";

	UpdateData(TRUE);

	m_colSource0Width = statsTab.m_SourceListCtrl.GetColumnWidth(0);
	m_colStats0Width = statsTab.m_StatsListCtrl.GetColumnWidth(0);
	m_colStats1Width = statsTab.m_StatsListCtrl.GetColumnWidth(1);
	m_colGStats0Width = statusTab.m_GlobalStatList.GetColumnWidth(0);
	m_colGStats1Width = statusTab.m_GlobalStatList.GetColumnWidth(1);
	m_colGStats2Width = statusTab.m_GlobalStatList.GetColumnWidth(2);


	sprintf(buf, "%d", m_colSource0Width);
	WritePrivateProfileString(gAppName, "col0SourceWidth", buf, gConfigFile);
	sprintf(buf, "%d", m_colStats0Width);
	WritePrivateProfileString(gAppName, "col0StatsWidth", buf, gConfigFile);
	sprintf(buf, "%d", m_colStats1Width);
	WritePrivateProfileString(gAppName, "col1StatsWidth", buf, gConfigFile);
	sprintf(buf, "%d", m_colGStats0Width);
	WritePrivateProfileString(gAppName, "col0GStatsWidth", buf, gConfigFile);
	sprintf(buf, "%d", m_colGStats1Width);
	WritePrivateProfileString(gAppName, "col1GStatsWidth", buf, gConfigFile);
	sprintf(buf, "%d", m_colGStats2Width);
	WritePrivateProfileString(gAppName, "col2GStatsWidth", buf, gConfigFile);

	if (m_Autostart) {
		WritePrivateProfileString(gAppName, "AutoStart", "1", gConfigFile);
	}
	else {
		WritePrivateProfileString(gAppName, "AutoStart", "0", gConfigFile);
	}

	sprintf(buf, "%d", gAdditionalGlobalStats.numStats);
	WritePrivateProfileString(gAppName, "numAdditionalStats", buf, gConfigFile);

	for (int i=0;i<gAdditionalGlobalStats.numStats;i++) {
		memset(buf, '\000', sizeof(buf));
		sprintf(buf2, "AdditionalStatsSource%d", i);
		WritePrivateProfileString(gAppName, buf2, gAdditionalGlobalStats.stats[i].source, gConfigFile);

		memset(buf, '\000', sizeof(buf));
		sprintf(buf2, "AdditionalStatsName%d", i);
		WritePrivateProfileString(gAppName, buf2, gAdditionalGlobalStats.stats[i].name, gConfigFile);
		gAdditionalGlobalStats.stats[i].name = buf;
	}

}

void CIcecast2winDlg::config_read()
{
	char	buf2[255] = "";
	char	buf[255] = "";
	CString	tempString;

	m_colSource0Width = GetPrivateProfileInt(gAppName, "col0SourceWidth", 150, gConfigFile);
	m_colStats0Width = GetPrivateProfileInt(gAppName, "col0StatsWidth", 100, gConfigFile);
	m_colStats1Width = GetPrivateProfileInt(gAppName, "col1StatsWidth", 150, gConfigFile);
	m_colGStats0Width = GetPrivateProfileInt(gAppName, "col0GStatsWidth", 150, gConfigFile);
	m_colGStats1Width = GetPrivateProfileInt(gAppName, "col1GStatsWidth", 150, gConfigFile);
	m_colGStats2Width = GetPrivateProfileInt(gAppName, "col2GStatsWidth", 150, gConfigFile);

	GetPrivateProfileString(gAppName, "AutoStart", "0", buf, sizeof(buf), gConfigFile);
	if (!strcmp(buf, "1")) {
		m_Autostart = true;
	}
	else{
		m_Autostart = false;
	}
	int numAdditionalGlobalStats = GetPrivateProfileInt(gAppName, "numAdditionalStats", 0, gConfigFile);
	for (int i=0;i<numAdditionalGlobalStats;i++) {
		memset(buf, '\000', sizeof(buf));
		sprintf(buf2, "AdditionalStatsSource%d", i);
		GetPrivateProfileString(gAppName, buf2, "", buf, sizeof(buf), gConfigFile);
		gAdditionalGlobalStats.stats[i].source = buf;

		memset(buf, '\000', sizeof(buf));
		sprintf(buf2, "AdditionalStatsName%d", i);
		GetPrivateProfileString(gAppName, buf2, "", buf, sizeof(buf), gConfigFile);
		gAdditionalGlobalStats.stats[i].name = buf;
		gAdditionalGlobalStats.numStats++;
	}

}

void CIcecast2winDlg::OnClose() 
{
	// TODO: Add your message handler code here and/or call default
	config_write();
	CResizableDialog::OnClose();
}

void CIcecast2winDlg::OnSize(UINT nType, int cx, int cy) 
{
	CResizableDialog::OnSize(nType, cx, cy);
	
	int border1 = 0;
	int border2 = 78;
	// TODO: Add your message handler code here
	if (m_MainTab.m_hWnd) {
		CRect rect;
		GetClientRect (&rect);
		m_MainTab.ResizeDialog(0, rect.Width()-border1, rect.Height()-border2);
		m_MainTab.ResizeDialog(1, rect.Width()-border1, rect.Height()-border2);
		m_MainTab.ResizeDialog(2, rect.Width()-border1, rect.Height()-border2);
	}

}
