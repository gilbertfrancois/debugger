// $Id$

#include "SlotViewer.h"
#include <QPainter>
#include <QPixmap>
#include <QPaintEvent>
#include <QStyleOptionHeader>

SlotViewer::SlotViewer( QWidget* parent )
	: QFrame( parent )
{
	setFrameStyle(WinPanel | Sunken);
	setFocusPolicy(Qt::StrongFocus);
	setBackgroundRole(QPalette::Base);

	memLayout = NULL;
	for(int p=0; p<4; p++) {
		slotsChanged[p] = FALSE;
		segmentsChanged[p] = FALSE;
	}

	frameL = frameT = frameB = frameWidth();
	frameR = frameL;

	setSizes();
}

void SlotViewer::resizeEvent(QResizeEvent *e)
{
	QFrame::resizeEvent(e);
}

void SlotViewer::paintEvent(QPaintEvent *e)
{
	// call parent for drawing the actual frame
	QFrame::paintEvent(e);
	
	QPainter p(this);

	QStyleOptionHeader so;
	so.init(this);
	so.state |= QStyle::State_Raised;
	so.orientation = Qt::Horizontal;
	so.position = QStyleOptionHeader::Beginning;
	so.sortIndicator = QStyleOptionHeader::None;
	so.textAlignment = Qt::AlignHCenter;
	so.rect.setTop(frameT);
	so.rect.setHeight(headerHeight);
	so.rect.setLeft(frameL);
	so.rect.setWidth(headerSize1);
	so.section = 0;
	so.text = "Page";
	style()->drawControl(QStyle::CE_Header, &so, &p);
	so.rect.setLeft(so.rect.left()+headerSize1);
	so.rect.setWidth(headerSize2);
	so.section = 1;
	so.text = "Address";
	style()->drawControl(QStyle::CE_Header, &so, &p);
	so.rect.setLeft(so.rect.left()+headerSize2);
	so.rect.setWidth(headerSize3);
	so.section = 2;
	so.text = "Slot";
	style()->drawControl(QStyle::CE_Header, &so, &p);
	so.rect.setLeft(so.rect.left()+headerSize3);
	so.rect.setWidth(headerSize4);
	so.section = 3;
	so.text = "Segment";
	style()->drawControl(QStyle::CE_Header, &so, &p);
	
	// calc and set drawing bounds
	QRect r( e->rect() );
	if(r.left()<frameL) r.setLeft(frameL);
	if(r.top()<frameT) r.setTop(frameT);
	if(r.right()>width()-frameR-1) r.setRight(width()-frameR-1);
	if(r.bottom()>height()-frameB-1) r.setBottom(height()-frameB-1);
	p.setClipRect(r);

	int	mid1 = frameL + headerSize1/2;
	int	mid2 = frameL + headerSize1 + headerSize2/2;
	int	mid3 = frameL + headerSize1 + headerSize2 + headerSize3/2;
	int	mid4 = frameL + headerSize1 + headerSize2 + headerSize3+ headerSize4/2;
	int dy = (height() - frameT - frameB - headerHeight)/4;
	int y = frameT + headerHeight + dy/2 + fontMetrics().height()/2 - fontMetrics().descent();

	QString str;
	int isOn = isEnabled() && memLayout!=NULL;
	
	for(int i=0; i<4; i++) {
		p.setPen( palette().color(QPalette::Text) );
		
		// print page nr
		str.sprintf("%i", i);
		p.drawText(mid1 - fontMetrics().width(str)/2, y, str);
		
		// print address
		str.sprintf("$%04X", i * 0x4000);
		p.drawText(mid2 - fontMetrics().width(str)/2, y, str);
		
		// print slot
		if(isOn) {
			if(memLayout->isSubslotted[memLayout->primarySlot[i] & 3])
				str.sprintf("%c-%c", memLayout->primarySlot[i], memLayout->secondarySlot[i]);
			else
				str = memLayout->primarySlot[i];
		} else {
			str = "-";
		}
		// set pen colour to red if slot was recently changed
		if(slotsChanged[i] && isOn)
			p.setPen( Qt::red );
		else
			p.setPen( palette().color(QPalette::Text) );
		
		p.drawText(mid3 - fontMetrics().width(str)/2, y, str);
		
		// print segment
		if(isOn) {
			int ms;
			if(memLayout->isSubslotted[memLayout->primarySlot[i] & 3])
				ms = memLayout->mapperSize[memLayout->primarySlot[i] & 3]
				                         [memLayout->secondarySlot[i] & 3];
			else
				ms = memLayout->mapperSize[memLayout->primarySlot[i] & 3][0];
			if(ms>0)
				str.sprintf("%i", memLayout->mapperSegment[i]);
			else
				str = "-";
		} else {
			str = "-";
		}
		// set pen colour to red if slot was recently changed
		if(segmentsChanged[i] && isOn)
			p.setPen( Qt::red );
		else
			p.setPen( palette().color(QPalette::Text) );
		p.drawText(mid4 - fontMetrics().width(str)/2, y, str);
		
		y += dy;
	}
}

void SlotViewer::setSizes()
{
	headerSize1 = 8 + fontMetrics().width("Page");
	headerSize2 = 8 + fontMetrics().width("Address");
	headerSize3 = 8 + fontMetrics().width("Slot");
	headerSize4 = 8 + fontMetrics().width("Segment");
	headerHeight = 8 + fontMetrics().height();
	
	int v = headerSize1+headerSize2+headerSize3+headerSize4+frameL+frameR;
	setMinimumWidth(v);
	setMaximumWidth(v);
}

void SlotViewer::refresh()
{
	CommCommandRequest *req = new CommCommandRequest(SLOTS_REQ_ID, "debug_memmapper");
	
	emit needUpdate(req);
}

void SlotViewer::setMemoryLayout(MemoryLayout *ml)
{
	memLayout = ml;
}

void SlotViewer::slotsUpdated(CommCommandRequest *r)
{
	QList<QByteArray> lines = r->result.split('\n');

	// parse page slots and segments
	for(int p=0; p<4; p++) {
		slotsChanged[p] = (memLayout->primarySlot[p] != lines[p*2][0]) ||
		                  (memLayout->secondarySlot[p] != lines[p*2][1]);
		memLayout->primarySlot[p] = lines[p*2][0];
		memLayout->secondarySlot[p] = lines[p*2][1];
		segmentsChanged[p] = memLayout->mapperSegment[p] != lines[p*2+1].toUShort();
		memLayout->mapperSegment[p] = lines[p*2+1].toUShort();
	}
	// parse slot layout
	int l=8;
	for(int ps=0; ps<4; ps++) {
		memLayout->isSubslotted[ps] = lines[l++][0]=='1';
		if(memLayout->isSubslotted[ps]) {
			for(int ss=0; ss<4; ss++)
				memLayout->mapperSize[ps][ss] = lines[l++].toUShort();
		} else {
			memLayout->mapperSize[ps][0] = lines[l++].toUShort();
		}
	}
	delete r;
	update();
}
