// $Id$

#include "DebuggerForm.h"
#include "DisasmViewer.h"
#include "HexViewer.h"
#include "CPURegsViewer.h"
#include "FlagsViewer.h"
#include "StackViewer.h"
#include "SlotViewer.h"
#include "CommClient.h"
#include "OpenMSXConnection.h"
#include "ConnectDialog.h"
#include "version.h"
#include <QAction>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QString>
#include <QStringList>
#include <QSplitter>
#include <QPixmap>



class QueryPauseHandler : public SimpleCommand
{
public:
	QueryPauseHandler(DebuggerForm& form_)
		: SimpleCommand("set pause")
		, form(form_)
	{
	}

	virtual void replyOk(const QString& message)
	{
		bool checked = message.trimmed() == "on";
		form.systemPauseAction->setChecked(checked);
		delete this;
	}
private:
	DebuggerForm& form;
};


class QueryBreakedHandler : public SimpleCommand
{
public:
	QueryBreakedHandler(DebuggerForm& form_)
		: SimpleCommand("debug breaked")
		, form(form_)
	{
	}

	virtual void replyOk(const QString& message)
	{
		form.finalizeConnection(message.trimmed() == "1");
		delete this;
	}
private:
	DebuggerForm& form;
};


class ListBreakPointsHandler : public SimpleCommand
{
public:
	ListBreakPointsHandler(DebuggerForm& form_)
		: SimpleCommand("debug list_bp")
		, form(form_)
	{
	}
	
	virtual void replyOk(const QString& message)
	{
		form.breakpoints.setBreakpoints(message);
		form.disasmView->update();
		delete this;
	}
private:
	DebuggerForm& form;
};


class CPURegRequest : public ReadDebugBlockCommand
{
public:
	CPURegRequest(DebuggerForm& form_)
		: ReadDebugBlockCommand("{CPU regs}", 0, 28, buf)
		, form(form_)
	{
	}

	virtual void replyOk(const QString& message)
	{
		copyData(message);
		form.regsView->setData(buf);
		delete this;
	}

private:
	DebuggerForm& form;
	unsigned char buf[28];
};


DebuggerForm::DebuggerForm(QWidget* parent)
	: QMainWindow(parent)
	, comm(CommClient::instance())
{
	createActions();
	createMenus();
	createToolbars();
	createStatusbar();
	createForm();
}

void DebuggerForm::createActions()
{
	systemConnectAction = new QAction(tr("&Connect"), this);
	systemConnectAction->setShortcut(tr("Ctrl+C"));
	systemConnectAction->setStatusTip(tr("Connect to openMSX"));
	systemConnectAction->setIcon(QIcon(":/icons/connect.png"));

	systemDisconnectAction = new QAction(tr("&Disconnect"), this);
	systemDisconnectAction->setShortcut(tr(""));
	systemDisconnectAction->setStatusTip(tr("Disconnect from openMSX"));
	systemDisconnectAction->setIcon(QIcon(":/icons/disconnect.png"));
	systemDisconnectAction->setEnabled(FALSE);
	
	systemPauseAction = new QAction(tr("&Pause emulator"), this);
	systemPauseAction->setShortcut(Qt::Key_Pause);
	systemPauseAction->setStatusTip(tr("Pause the emulation"));
	systemPauseAction->setIcon(QIcon(":/icons/pause.png"));
	systemPauseAction->setCheckable(TRUE);
	systemPauseAction->setEnabled(FALSE);
	
	systemExitAction = new QAction(tr("E&xit"), this);
	systemExitAction->setShortcut(tr("Alt+X"));
	systemExitAction->setStatusTip(tr("Quit the openMSX debugger"));
	
	executeBreakAction = new QAction(tr("Break"), this);
	executeBreakAction->setShortcut(tr("CRTL+B"));
	executeBreakAction->setStatusTip(tr("Halt the execution and enter debug mode"));
	executeBreakAction->setIcon(QIcon(":/icons/break.png"));
	executeBreakAction->setEnabled(FALSE);
	
	executeRunAction = new QAction(tr("Run"), this);
	executeRunAction->setShortcut(tr("F9"));
	executeRunAction->setStatusTip(tr("Leave debug mode and resume execution"));
	executeRunAction->setIcon(QIcon(":/icons/run.png"));
	executeRunAction->setEnabled(FALSE);

	executeStepAction = new QAction(tr("Step into"), this);
	executeStepAction->setShortcut(tr("F7"));
	executeStepAction->setStatusTip(tr("Execute a single instruction"));
	executeStepAction->setIcon(QIcon(":/icons/stepinto.png"));
	executeStepAction->setEnabled(FALSE);

	executeStepOverAction = new QAction(tr("Step over"), this);
	executeStepOverAction->setShortcut(tr("F8"));
	executeStepOverAction->setStatusTip(tr("Execute the next instruction including any called subroutines"));
	executeStepOverAction->setIcon(QIcon(":/icons/stepover.png"));
	executeStepOverAction->setEnabled(FALSE);

	executeStepOutAction = new QAction(tr("Step out"), this);
	executeStepOutAction->setShortcut(tr("F11"));
	executeStepOutAction->setStatusTip(tr("Resume execution until the current routine has finished"));
	executeStepOutAction->setIcon(QIcon(":/icons/stepout.png"));
	executeStepOutAction->setEnabled(FALSE);

	executeRunToAction = new QAction(tr("Run to"), this);
	executeRunToAction->setShortcut(tr("F4"));
	executeRunToAction->setStatusTip(tr("Resume execution until the selected line is reached"));
	executeRunToAction->setIcon(QIcon(":/icons/runto.png"));
	executeRunToAction->setEnabled(FALSE);

	breakpointToggleAction = new QAction(tr("Toggle"), this);
	breakpointToggleAction->setShortcut(tr("F5"));
	breakpointToggleAction->setStatusTip(tr("Toggle breakpoint on/off at cursor"));
	breakpointToggleAction->setIcon(QIcon(":/icons/break.png"));
	breakpointToggleAction->setEnabled(FALSE);

	helpAboutAction = new QAction(tr("&About"), this);
	executeRunToAction->setStatusTip(tr("Show the appliction information"));

	connect( systemConnectAction, SIGNAL( triggered() ), this, SLOT( systemConnect() ) );
	connect( systemDisconnectAction, SIGNAL( triggered() ), this, SLOT( systemDisconnect() ) );
	connect( systemPauseAction, SIGNAL( triggered() ), this, SLOT( systemPause() ) );
	connect( systemExitAction, SIGNAL( triggered() ), this, SLOT( close() ) );
	connect( executeBreakAction, SIGNAL( triggered() ), this, SLOT( executeBreak() ) );
	connect( executeRunAction, SIGNAL( triggered() ), this, SLOT( executeRun() ) );
	connect( executeStepAction, SIGNAL( triggered() ), this, SLOT( executeStep() ) );
	connect( executeStepOverAction, SIGNAL( triggered() ), this, SLOT( executeStepOver() ) );
	connect( executeRunToAction, SIGNAL( triggered() ), this, SLOT( executeRunTo() ) );
	connect( executeStepOutAction, SIGNAL( triggered() ), this, SLOT( executeStepOut() ) );
	connect( breakpointToggleAction, SIGNAL( triggered() ), this, SLOT( breakpointToggle() ) );
	connect( helpAboutAction, SIGNAL( triggered() ), this, SLOT( showAbout() ) );
}

void DebuggerForm::createMenus()	
{
	// create system menu
	systemMenu = menuBar()->addMenu(tr("&System"));
	systemMenu->addAction(systemConnectAction);
	systemMenu->addAction(systemDisconnectAction);
	systemMenu->addSeparator();
	systemMenu->addAction(systemPauseAction);
	systemMenu->addSeparator();
	systemMenu->addAction(systemExitAction);

	// create execute menu
	executeMenu = menuBar()->addMenu(tr("&Execute"));
	executeMenu->addAction(executeBreakAction);
	executeMenu->addAction(executeRunAction);
	executeMenu->addSeparator();
	executeMenu->addAction(executeStepAction);
	executeMenu->addAction(executeStepOverAction);
	executeMenu->addAction(executeStepOutAction);
	executeMenu->addAction(executeRunToAction);

	// create breakpoint menu
	breakpointMenu = menuBar()->addMenu(tr("&Breakpoint"));
	breakpointMenu->addAction(breakpointToggleAction);

	// create help menu
	helpMenu = menuBar()->addMenu(tr("&Help"));
	helpMenu->addAction(helpAboutAction);
}

void DebuggerForm::createToolbars()	
{
	// create debug toolbar
	systemToolbar = addToolBar(tr("System"));
	systemToolbar->addAction(systemConnectAction);
	systemToolbar->addAction(systemDisconnectAction);
	systemToolbar->addSeparator();
	systemToolbar->addAction(systemPauseAction);

	// create debug toolbar
	executeToolbar = addToolBar(tr("Execution"));
	executeToolbar->addAction(executeBreakAction);
	executeToolbar->addAction(executeRunAction);
	executeToolbar->addSeparator();
	executeToolbar->addAction(executeStepAction);
	executeToolbar->addAction(executeStepOverAction);
	executeToolbar->addAction(executeStepOutAction);
	executeToolbar->addAction(executeRunToAction);
}

void DebuggerForm::createStatusbar()	
{
	// create the statusbar
	statusBar()->showMessage("No emulation running.");
}

QWidget *DebuggerForm::createNamedWidget(const QString& name, QWidget *widget)
{
	QVBoxLayout *vboxLayout = new QVBoxLayout;
	vboxLayout->setMargin(3);
	vboxLayout->setSpacing(2);
	
	QLabel *lbl = new QLabel(name);

	vboxLayout->addWidget(lbl, 0);
	vboxLayout->addWidget(widget, 1);
	
	QWidget *combined = new QWidget;
	combined->setLayout(vboxLayout);

	return combined;
}

void DebuggerForm::createForm()	
{
	mainSplitter = new QSplitter(Qt::Horizontal, this);
	setCentralWidget(mainSplitter);

	// * create the left side of the gui

	disasmSplitter = new QSplitter(Qt::Vertical, this);
	mainSplitter->addWidget(disasmSplitter);
	
	// create the disasm viewer widget
	disasmView = new DisasmViewer();
	disasmSplitter->addWidget(createNamedWidget(tr("Code view:"), disasmView));
	
	// create the memory view widget
	hexView = new HexViewer();
	disasmSplitter->addWidget(createNamedWidget(tr("Main memory:"), hexView));
	
	// * create the right side of the gui
	QWidget *w = new QWidget;
	mainSplitter->addWidget(w);
	QVBoxLayout *rightLayout = new QVBoxLayout;
	rightLayout->setMargin(0);
	rightLayout->setSpacing(0);
	w->setLayout(rightLayout);

	QWidget *w2 = new QWidget;
	rightLayout->addWidget(w2, 0);	
	QHBoxLayout *topLayout = new QHBoxLayout;
	topLayout->setMargin(0);
	topLayout->setSpacing(0);
	w2->setLayout(topLayout);
	
	// create register viewer
	regsView = new CPURegsViewer;
	topLayout->addWidget(createNamedWidget(tr("CPU registers:"), regsView), 0);
	
	// create flags viewer
	flagsView = new FlagsViewer();
	topLayout->addWidget(createNamedWidget(tr("Flags:"), flagsView), 0);

	// create stack viewer
	stackView = new StackViewer();
	topLayout->addWidget(createNamedWidget(tr("Stack:"), stackView), 0);

	// create slot viewer
	slotView = new SlotViewer();
	topLayout->addWidget(createNamedWidget(tr("Memory layout:"), slotView), 0);

	// create spacer on the right
	w2 = new QWidget();
	topLayout->addWidget(w2, 1);

	// create rest
	w2 = new QWidget();
	rightLayout->addWidget(w2, 1);
	
	disasmView->setEnabled(FALSE);
	hexView->setEnabled(FALSE);
	regsView->setEnabled(FALSE);
	flagsView->setEnabled(FALSE);
	stackView->setEnabled(FALSE);
	slotView->setEnabled(FALSE);

	connect(regsView,   SIGNAL( pcChanged(quint16) ),
	        disasmView, SLOT( setProgramCounter(quint16) ) );
	connect(regsView,   SIGNAL( flagsChanged(quint8) ),
	        flagsView,  SLOT( setFlags(quint8) ) );
	connect(regsView,   SIGNAL( spChanged(quint16) ),
	        stackView,  SLOT( setStackPointer(quint16) ) );	
	connect(disasmView, SIGNAL( toggleBreakpoint(int) ),
	                    SLOT( breakpointToggle(int) ) );

	connect(&comm, SIGNAL( connectionReady() ),
	               SLOT( initConnection() ) );
	connect(&comm, SIGNAL(updateParsed(const QString&, const QString&, const QString&)),
	               SLOT(handleUpdate(const QString&, const QString&, const QString&)));
	connect(&comm, SIGNAL( connectionTerminated() ),
	               SLOT( connectionClosed() ) );

	// init main memory
	// added four bytes as runover buffer for dasm
	// otherwise dasm would need to check the buffer end continously.
	breakpoints.setMemoryLayout(&memLayout);
	mainMemory = new unsigned char[65536+4];
	memset(mainMemory, 0, 65536+4);
	disasmView->setMemory(mainMemory);
	disasmView->setBreakpoints(&breakpoints);
	hexView->setData("memory", mainMemory, 65536);
	stackView->setData(mainMemory, 65536);
	slotView->setMemoryLayout(&memLayout);
}

DebuggerForm::~DebuggerForm()
{
	delete[] mainMemory;
}

void DebuggerForm::initConnection()
{
	comm.sendCommand(new QueryPauseHandler(*this));
	comm.sendCommand(new QueryBreakedHandler(*this));

	comm.sendCommand(new SimpleCommand("update enable status"));

	// define 'debug_bin2hex' proc for internal use
	comm.sendCommand(new SimpleCommand(
		"proc debug_bin2hex { input } {\n"
		"  set result \"\"\n"
		"  foreach i [split $input {}] {\n"
		"    append result [format %02X [scan $i %c]] \"\"\n"
		"  }\n"
		"  return $result\n"
		"}\n"));

	// define 'debug_memmapper' proc for internal use
	comm.sendCommand(new SimpleCommand(
		"proc debug_memmapper { } {\n"
		"  set result \"\"\n"
		"  for { set page 0 } { $page &lt; 4 } { incr page } {\n"
		"    set tmp [get_selected_slot $page]\n"
		"    append result [lindex $tmp 0] [lindex $tmp 1] \"\\n\"\n"
		"    if { [lsearch [debug list] \"MapperIO\"] != -1} {\n"
		"      append result [debug read \"MapperIO\" $page] \"\\n\"\n"
		"    } else {\n"
		"      append result \"0\\n\"\n"
		"    }\n"
		"  }\n"
		"  for { set ps 0 } { $ps &lt; 4 } { incr ps } {\n"
		"    if [openmsx_info issubslotted $ps] {\n"
		"      append result \"1\\n\"\n"
		"      for { set ss 0 } { $ss &lt; 4 } { incr ss } {\n"
		"        append result [get_mapper_size $ps $ss] \"\\n\"\n"
		"      }\n"
		"    } else {\n"
		"      append result \"0\\n\"\n"
		"      append result [get_mapper_size $ps 0] \"\\n\"\n"
		"    }\n"
		"  }\n"
		"  return $result\n"
		"}\n"));
}

void DebuggerForm::connectionClosed()
{
	systemDisconnectAction->setEnabled(FALSE);
	systemPauseAction->setEnabled(FALSE);
	executeBreakAction->setEnabled(FALSE);
	executeRunAction->setEnabled(FALSE);
	executeStepAction->setEnabled(FALSE);
	executeStepOverAction->setEnabled(FALSE);
	executeStepOutAction->setEnabled(FALSE);
	executeRunToAction->setEnabled(FALSE);
	systemConnectAction->setEnabled(TRUE);

	disasmView->setEnabled(FALSE);
	hexView->setEnabled(FALSE);
	regsView->setEnabled(FALSE);
	flagsView->setEnabled(FALSE);
	stackView->setEnabled(FALSE);
	slotView->setEnabled(FALSE);
}

void DebuggerForm::finalizeConnection(bool halted)
{
	systemDisconnectAction->setEnabled(TRUE);
	systemPauseAction->setEnabled(TRUE);
	if(halted){
		setBreakMode();
		breakOccured();
	}else
		setRunMode();

	disasmView->setEnabled(TRUE);
	hexView->setEnabled(TRUE);
	regsView->setEnabled(TRUE);
	flagsView->setEnabled(TRUE);
	stackView->setEnabled(TRUE);
	slotView->setEnabled(TRUE);
}

void DebuggerForm::handleUpdate(const QString& type, const QString& name,
                                const QString& message)
{
	if (type == "status") {
		if (name == "cpu") {
			if (message == "suspended") {
				breakOccured();
			} else {
				setRunMode();
			}
		} else if (name == "paused") {
			pauseStatusChanged(message == "true");
		}
	}
}

void DebuggerForm::pauseStatusChanged(bool isPaused) 
{
	systemPauseAction->setChecked(isPaused);
}

void DebuggerForm::breakOccured()
{
	setBreakMode();
	comm.sendCommand(new ListBreakPointsHandler(*this));

	// update registers 
	// note that a register update is processed, a signal is sent to other
	// widgets as well. Any dependent updates shoud be called before this one.
	CPURegRequest *regs = new CPURegRequest(*this);
	comm.sendCommand(regs);
	
	// refresh memory viewer
	hexView->refresh();

	// refresh slot viewer
	slotView->refresh();
}

void DebuggerForm::setBreakMode()
{
	executeBreakAction->setEnabled(FALSE);
	executeRunAction->setEnabled(TRUE);
	executeStepAction->setEnabled(TRUE);
	executeStepOverAction->setEnabled(TRUE);
	executeStepOutAction->setEnabled(TRUE);
	executeRunToAction->setEnabled(TRUE);
	breakpointToggleAction->setEnabled(TRUE);
}

void DebuggerForm::setRunMode()
{
	executeBreakAction->setEnabled(TRUE);
	executeRunAction->setEnabled(FALSE);
	executeStepAction->setEnabled(FALSE);
	executeStepOverAction->setEnabled(FALSE);
	executeStepOutAction->setEnabled(FALSE);
	executeRunToAction->setEnabled(FALSE);
	breakpointToggleAction->setEnabled(FALSE);
}

void DebuggerForm::systemConnect()
{
	systemConnectAction->setEnabled(FALSE);
	OpenMSXConnection* connection = ConnectDialog::getConnection(this);
	if (connection) {
		comm.connectToOpenMSX(connection);
	}
}

void DebuggerForm::systemDisconnect()
{
	comm.closeConnection();
}

void DebuggerForm::systemPause()
{
	comm.sendCommand(new SimpleCommand(QString("set pause ") +
	                    (systemPauseAction->isChecked() ? "true" : "false")));
}

void DebuggerForm::executeBreak()
{
	comm.sendCommand(new SimpleCommand("debug break"));
}

void DebuggerForm::executeRun()
{
	comm.sendCommand(new SimpleCommand("debug cont"));
	setRunMode();
}

void DebuggerForm::executeStep()
{
	comm.sendCommand(new SimpleCommand("debug step"));
	setRunMode();
}

void DebuggerForm::executeStepOver()
{
	comm.sendCommand(new SimpleCommand("step_over"));
	setRunMode();
}

void DebuggerForm::executeRunTo()
{
	comm.sendCommand(new SimpleCommand(
	                  "run_to " + QString::number(disasmView->cursorAddr)));
	setRunMode();
}

void DebuggerForm::executeStepOut()
{
	// TODO
}

void DebuggerForm::breakpointToggle(int addr)
{
	// TODO move this test out of this function???
	if (addr < 0) addr = disasmView->cursorAddr;
	
	QString cmd;
	if (breakpoints.isBreakpoint(addr)) {
		cmd = "debug remove_bp " + breakpoints.idString(addr);
	} else {
		int p = (addr & 0xC000) >> 14;
		cmd.sprintf("debug set_bp %i { [ pc_in_slot %c %c %i ] }",
		            addr, memLayout.primarySlot[p],
		            memLayout.secondarySlot[p],
		            memLayout.mapperSegment[p]);
	}
	comm.sendCommand(new SimpleCommand(cmd));

	comm.sendCommand(new ListBreakPointsHandler(*this));
}

void DebuggerForm::showAbout()
{
	QString s;
	s.sprintf("openMSX debugger %i.%i.%i", VERSION_MAJOR,
	                                       VERSION_MINOR,
	                                       VERSION_PATCH);

	QMessageBox::about(this, "openMSX", s);
}
