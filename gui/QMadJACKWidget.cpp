/*

	QMadJACKWidget.cpp
	
	QT Interface to MadJACK
	Copyright (C) 2006  Nicholas J. Humfrey
	
	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <QDebug>
#include <QWidget>
#include <QPushButton>

#include "QMadJACKWidget.h"


QMadJACKWidget::QMadJACKWidget(  QMadJACK *in_madjack, QWidget *parent )
		: QWidget(parent)
{
	madjack = in_madjack;

	init();
}


QMadJACKWidget::~QMadJACKWidget()
{


}





void QMadJACKWidget::init()
{
    QPushButton *play = new QPushButton(tr("Play"), this);

    play->setGeometry(62, 40, 75, 30);
//	play->show();
}

