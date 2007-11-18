/*
 * tabdlg.cpp - dialog for handling tabbed chats
 * Copyright (C) 2005  Kevin Smith
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "tabdlg.h"

#include "iconwidget.h"
#include "iconset.h"
#include "common.h"
#include "psicon.h"
#include <qmenubar.h>
#include <qcursor.h>
#include <q3dragobject.h>
#include <QVBoxLayout>
#include <QDragMoveEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <Q3PopupMenu>
#include <QDropEvent>
#include <QCloseEvent>
#include "psitabwidget.h"
#include "psioptions.h"
#include "shortcutmanager.h"
#include "chatdlg.h"
#include "tabmanager.h"

#ifdef Q_WS_WIN
#include <windows.h>
#endif

//----------------------------------------------------------------------------
// TabDlg
//----------------------------------------------------------------------------
TabDlg::TabDlg(TabManager* tabManager) : tabManager_(tabManager)
{
  	if ( option.brushedMetal )
		setAttribute(Qt::WA_MacMetalStyle);

	tabMenu = new QMenu( this );
	
	tabs = new PsiTabWidget (this);
	tabs->setCloseIcon(IconsetFactory::icon("psi/closetab").icon());
	//tabs->setCloseIcon(IconsetFactory::icon("psi/closetab").iconSet());
	connect (tabs, SIGNAL( mouseDoubleClickTab( QWidget* ) ), SLOT( detachChat( QWidget* ) ) );
	connect (tabs, SIGNAL(aboutToShowMenu(QMenu *)), SLOT(tab_aboutToShowMenu(QMenu *)));
	connect (tabs, SIGNAL(tabContextMenu(int,QPoint,QContextMenuEvent*)), SLOT(showTabMenu(int,QPoint,QContextMenuEvent*)));

	QVBoxLayout *vert1 = new QVBoxLayout( this, 1);
	vert1->addWidget(tabs);
	X11WM_CLASS("chat");
	
	connect( tabs, SIGNAL( closeButtonClicked() ), SLOT( closeChat() ) );
	connect( tabs, SIGNAL( currentChanged( QWidget* ) ), SLOT( tabSelected( QWidget* ) ) );	

	setAcceptDrops(TRUE);

	setLooks();

	resize(option.sizeTabDlg);

	act_close = new QAction(this);
	addAction(act_close);
	connect(act_close,SIGNAL(activated()), SLOT(closeChat()));
	act_prev = new QAction(this);
	addAction(act_prev);
	connect(act_prev,SIGNAL(activated()), SLOT(previousTab()));
	act_next = new QAction(this);
	addAction(act_next);
	connect(act_next,SIGNAL(activated()), SLOT(nextTab()));

	setShortcuts();
}

TabDlg::~TabDlg()
{

}

// FIXME: This is a bad idea to store pointers in QMimeData
Q_DECLARE_METATYPE ( TabDlg* );

void TabDlg::setShortcuts()
{
	//act_close->setShortcuts(ShortcutManager::instance()->shortcuts("common.close"));
	act_prev->setShortcuts(ShortcutManager::instance()->shortcuts("chat.previous-tab"));
	act_next->setShortcuts(ShortcutManager::instance()->shortcuts("chat.next-tab"));
}

void TabDlg::resizeEvent(QResizeEvent *e)
{
  if(option.keepSizes)
	option.sizeTabDlg = e->size();
}

void TabDlg::showTabMenu(int tab, QPoint pos, QContextMenuEvent * event)
{
	Q_UNUSED(event);
	tabMenu->clear();

	if (tab!=-1) {
		QAction *d = tabMenu->addAction(tr("Detach Tab"));
		QAction *c = tabMenu->addAction(tr("Close Tab"));

		QMenu* sendTo = new QMenu(tabMenu);
		sendTo->setTitle(tr("Send Tab to"));
		QMap<QAction*, TabDlg*> sentTos;
		for (uint i = 0; i < tabManager_->getTabSets()->count(); ++i)
		{
			TabDlg* tabSet= tabManager_->getTabSets()->at(i);
			QAction *act = sendTo->addAction( tabSet->getName());
			if (tabSet == this) act->setEnabled(false);
			sentTos[act] = tabSet;
		}
		tabMenu->addMenu(sendTo);

		QAction *act = tabMenu->exec(pos);
		if (!act) return;
		if (act == c) {
			closeChat(getTab(tab));
		} else if (act == d) {
			detachChat(getTab(tab));
		} else {
			TabDlg* target = sentTos[act];
			if (target) queuedSendChatTo(getTab(tab), target);
		}
	}
}

void TabDlg::tab_aboutToShowMenu(QMenu *menu)
{
	menu->addSeparator ();
	menu->addAction( tr("Detach Current Tab"), this, SLOT( detachChat() ) );
	menu->addAction( tr("Close Current Tab"), this, SLOT( closeChat() ) );

	QMenu* sendTo = new QMenu(menu);
	sendTo->setTitle(tr("Send Current Tab to"));
	int tabdlgmetatype = qRegisterMetaType<TabDlg*>("TabDlg*");
	for (uint i = 0; i < tabManager_->getTabSets()->count(); ++i)
	{
		TabDlg* tabSet= tabManager_->getTabSets()->at(i);
		QAction *act = sendTo->addAction( tabSet->getName());
		act->setData(QVariant(tabdlgmetatype, &tabSet));
		if (tabSet == this) act->setEnabled(false);
	}
	connect(sendTo, SIGNAL(triggered(QAction*)), SLOT(menu_sendChatTo(QAction*)));
	menu->addMenu(sendTo);
}

void TabDlg::menu_sendChatTo(QAction *act)
{
	queuedSendChatTo(tabs->currentPage(), act->data().value<TabDlg*>());
}

void TabDlg::sendChatTo(QWidget* chatw, TabDlg* otherTabs)
{
	if (otherTabs==this)
		return;
	TabbableWidget* chat = (TabbableWidget*)chatw;
	closeTab(chat, false);
	otherTabs->addTab(chat);
}

void TabDlg::queuedSendChatTo(QWidget* chat, TabDlg *dest)
{
	qRegisterMetaType<TabDlg*>("TabDlg*");
	QMetaObject::invokeMethod(this, "sendChatTo",  Qt::QueuedConnection, Q_ARG(QWidget*, chat), Q_ARG(TabDlg*, dest));
}

void TabDlg::optionsUpdate()
{
	setShortcuts();
}

void TabDlg::setLooks()
{
	//set the widget icon
#ifndef Q_WS_MAC
	setWindowIcon(IconsetFactory::icon("psi/start-chat").icon());
#endif
	tabs->setTabPosition(QTabWidget::Top);
	if (option.putTabsAtBottom)
		tabs->setTabPosition(QTabWidget::Bottom);

	setWindowOpacity(double(qMax(MINIMUM_OPACITY,PsiOptions::instance()->getOption("options.ui.chat.opacity").toInt()))/100);
}

QString TabDlg::getName() const
{
	return ((TabbableWidget*)(tabs->currentPage()))->getDisplayName();
}

void TabDlg::tabSelected(QWidget* chat)
{
	if (!chat) return; // FIXME
	((TabbableWidget*)chat)->activated(); //is this still necessary?
	updateCaption();
}

bool TabDlg::managesTab(const TabbableWidget* chat) const
{
	if (chats.contains(const_cast<TabbableWidget*>(chat)))
		return true;
	return false;
}

bool TabDlg::tabOnTop(const TabbableWidget* chat) const
{
	if ( tabs->currentPage() == chat )
		return true;
	return false;
}

void TabDlg::addTab(TabbableWidget* tab)
{
	chats.append(tab);
	QString tablabel = tab->getDisplayName();
	tablabel.replace("&", "&&");
	tabs->addTab(tab, tablabel);
	//tabs->setTabIconSet(tab, IconsetFactory::icon("psi/start-chat").icon());

	//tabs->showPage(tab);
	connect(tab, SIGNAL(captionChanged(QString)), SLOT(updateTab(QString)));
	connect(tab, SIGNAL(contactStateChanged(XMPP::ChatState)), SLOT(setTabState(XMPP::ChatState)));
	connect(tab, SIGNAL(unreadEventUpdate(int)), SLOT(setTabHasEvents(int)));
	connect(tab, SIGNAL(updateFlashState()), SLOT(updateFlashState()));

	this->show();
	updateCaption();
}

void TabDlg::detachChat()
{
	detachChat(tabs->currentPage());
}

void TabDlg::detachChat(QWidget* chat)
{
	//don't detach singleton chats, fix for flyspray #477
	if (tabs->count()==1)
		return;
	
	if (!chat) { // fail gracefully this is delayed/signaled user input.
		return;
	}

	TabDlg *newTab = tabManager_->newTabs();
	sendChatTo(chat, newTab);
}

void TabDlg::closeChat()
{
	TabbableWidget* chat = (TabbableWidget*)(tabs->currentPage());
	closeChat(chat);
}

/**
 * Call this when you want a tab to be removed immediately with no readiness checks
 * or reparenting, hiding etc (Such as on tab destruction).
 */ 
void TabDlg::removeTabWithNoChecks(TabbableWidget *tab)
{
	disconnect(tab, SIGNAL(captionChanged(QString)), this, SLOT(updateTab(TabbableWidget*)));
	disconnect(tab, SIGNAL(contactStateChanged(XMPP::ChatState)), this, SLOT(setTabState(XMPP::ChatState)));
	disconnect(tab, SIGNAL(unreadEventUpdate(int)), this, SLOT(setTabHasEvents(int)));
	disconnect(tab, SIGNAL(updateFlashState()), this, SLOT(updateFlashState()));

	tabs->removePage(tab);
	tabIsComposing.erase(tab);
	tabHasMessages.erase(tab);
	chats.removeAll(tab);
}

/**
 * Removes the chat from the tabset, 'closing' it if specified.
 * The method is used without closing tabs when transferring from one
 * tabset to another.
 * \param chat Chat to remove.
 * \param doclose Whether the chat is 'closed' while removing it.
 */ 
void TabDlg::closeTab(TabbableWidget* chat, bool doclose=true)
{
	if (doclose && !chat->readyToHide()) {
		return;
	}
	chat->hide();
	removeTabWithNoChecks(chat);
	chat->reparent(0,QPoint());
	if (doclose && chat->testAttribute(Qt::WA_DeleteOnClose)) {
		chat->close();
	}
	if (tabs->count()>0) {
		updateCaption();
	}
	checkHasChats();
}

void TabDlg::closeChat(QWidget* chat)
{
	closeTab((TabbableWidget*)chat);
}

void TabDlg::selectTab(TabbableWidget* chat)
{
	tabs->showPage(chat);
}

void TabDlg::checkHasChats()
{
	if (tabs->count()>0)
		return;
	closeMe();
}

void TabDlg::windowActivationChange(bool oldstate)
{
	QWidget::windowActivationChange(oldstate);

	// if we're bringing it to the front, get rid of the '*' if necessary
	if( isActiveWindow() ) { 
		activated();
	}
}

void TabDlg::activated()
{
	updateCaption();
	extinguishFlashingTabs();
}

void TabDlg::updateCaption()
{
	QString cap = "";
	uint pending = 0;
	for (int i = 0; i < tabHasMessages.count(); ++i) {
		pending += tabHasMessages.values()[i];
	}
	if (pending > 0) {
		cap += "* ";
		if (pending > 1)
			cap += QString("[%1] ").arg(pending);
	}
	cap += getName();
	if (tabIsComposing[(TabbableWidget*)(tabs->currentPage())])
		cap += tr(" is composing");

	setWindowTitle(cap);
}

void TabDlg::closeEvent(QCloseEvent* closeEvent)
{
	Q_UNUSED(closeEvent);
	int count=tabs->count();
	for (int i=0;i<count;++i) {
		closeChat();
	}
}

void TabDlg::closeMe()
{
	emit isDying(this);
	//we do not delete it here, let the PsiCon do that, they create, they destroy.
}


TabbableWidget *TabDlg::getTab(int i) const
{
	return ((TabbableWidget*)tabs->page(i));
}


TabbableWidget* TabDlg::getTabPointer(QString fullJid)
{
	for (int i=0; i < tabs->count() ; i++)
	{
		if (getTab(i)->jid().full()==fullJid)
		{
			return getTab(i);
		}
	}
	return false;
}

void TabDlg::updateTab(QString caption)
{
	TabbableWidget *tab = qobject_cast<TabbableWidget*>(sender());
	updateTab(tab);
}

void TabDlg::updateTab(TabbableWidget* chat)
{
	QString label, prefix;
	int num = tabHasMessages[chat];
	if (num == 0) {
		prefix = "";
	}
	else if (num == 1) {
		prefix = "* ";
	}
	else {
		prefix = QString("[%1] ").arg(num);
	}

	label = prefix + chat->getDisplayName();
	label.replace("&", "&&");
	tabs->setTabLabel(chat, label);
	//now set text colour based upon whether there are new messages/composing etc

	if (tabIsComposing[chat]) {
		tabs->setTabTextColor(chat, Qt::darkGreen);
	}
	else if (tabHasMessages[chat]) {
		tabs->setTabTextColor(chat, Qt::red);
	}
	else {
		tabs->setTabTextColor(chat, colorGroup().foreground());
	}
	updateCaption();
}

void TabDlg::setTabState( XMPP::ChatState state )
{
	TabbableWidget* chat = (TabbableWidget*) sender();
	if ( state == XMPP::StateComposing )
		tabIsComposing[chat] = true;
	else
		tabIsComposing[chat] = false;
	updateTab(chat);
}

void TabDlg::setTabHasEvents(int messages)
{
	TabbableWidget* chat = qobject_cast<TabbableWidget*>(sender());
	tabHasMessages[chat] = messages;
	updateTab(chat);
}

void TabDlg::nextTab()
{
	int page = tabs->currentPageIndex()+1;
	if ( page >= tabs->count() )
		page = 0;
	tabs->setCurrentPage( page );
}

void TabDlg::previousTab()
{
	int page = tabs->currentPageIndex()-1;
	if ( page < 0 )
		page = tabs->count() - 1;
	tabs->setCurrentPage( page );
}

void TabDlg::keyPressEvent(QKeyEvent *e)
{
	if (e->key() == Qt::Key_Escape) {
		closeChat();
	}
	else if (e->key() == Qt::Key_W && (e->modifiers() & Qt::ControlModifier)) {
		closeChat();
	}
	else {
		e->ignore();
	}
}

void TabDlg::dragEnterEvent(QDragEnterEvent *event)
{
	if ( event->mimeData()->hasFormat("psiTabDrag") ) {
		event->setDropAction(Qt::MoveAction);
		event->accept();
	}
}

void TabDlg::dropEvent(QDropEvent *event)
{
	QByteArray data;
	if (event->mimeData()->hasFormat("psiTabDrag")) {
		data = event->mimeData()->data("psiTabDrag");
	} else {
		return;
	}
	int remoteTab = data.toInt();
	event->acceptProposedAction();
	//the event's been and gone, now do something about it
	PsiTabBar* source = dynamic_cast<PsiTabBar*> (event->source());
	if (source)
	{
		PsiTabWidget* barParent = source->psiTabWidget();
		QWidget* widget = barParent->widget(remoteTab);
		TabbableWidget* chat=dynamic_cast<TabbableWidget*>(widget);
		TabDlg *dlg = tabManager_->getManagingTabs(chat);
		if (!chat || !dlg)
			return;
		dlg->queuedSendChatTo(chat, this);
	} 
	
}

void TabDlg::extinguishFlashingTabs()
{
	foreach(TabbableWidget* tab, chats) {
		if (tab->flashing()) {
			tab->blockSignals(true);
			tab->doFlash(false);
			tab->blockSignals(false);
		}
	}

	updateFlashState();
}

void TabDlg::updateFlashState()
{
	bool flash = false;
	foreach(TabbableWidget* tab, chats) {
		if (tab->flashing()) {
			flash = true;
			break;
		}
	}

	flash = flash && !isActiveWindow();
	doFlash(flash);
}