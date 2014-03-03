/*=========================================================================

  Library:   CTK

  Copyright (c) Kitware Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  =========================================================================*/

#ifndef __ctkLayoutManager_p_h
#define __ctkLayoutManager_p_h

// Qt includes
#include <QDomDocument>
#include <QObject>
#include <QSet>

class QLayout;
class QWidget;

// CTK includes
#include "ctkLayoutManager.h"

//-----------------------------------------------------------------------------
/// \ingroup Widgets
class CTK_WIDGETS_EXPORT ctkLayoutManagerPrivate
{
  Q_DECLARE_PUBLIC(ctkLayoutManager);

protected:
  ctkLayoutManager* const q_ptr;

public:
  ctkLayoutManagerPrivate(ctkLayoutManager& object);
  virtual ~ctkLayoutManagerPrivate();

  virtual void init();
  void clearLayout(QLayout* layout);
  void clearWidget(QWidget* widget, QLayout* parentLayout = 0);

  QWidget*       Viewport;
  QDomDocument   Layout;
  QSet<QWidget*> Views;
  QSet<QWidget*> LayoutWidgets;
  int            Spacing;
};

#endif
