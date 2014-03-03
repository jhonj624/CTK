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

// std includes
#include <iostream>

#include <dcmimage.h>

// Qt includes
#include <QAction>
#include <QCoreApplication>
#include <QCheckBox>
#include <QDebug>
#include <QMessageBox>
#include <QMetaType>
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QProgressDialog>
#include <QSettings>
#include <QSlider>
#include <QTabBar>
#include <QTimer>
#include <QTreeView>

// ctkWidgets includes
#include "ctkDirectoryButton.h"
#include "ctkFileDialog.h"

// ctkDICOMCore includes
#include "ctkDICOMDatabase.h"
#include "ctkDICOMFilterProxyModel.h"
#include "ctkDICOMIndexer.h"
#include "ctkDICOMModel.h"

// ctkDICOMWidgets includes
#include "ctkDICOMBrowser.h"
#include "ctkDICOMThumbnailGenerator.h"
#include "ctkThumbnailLabel.h"
#include "ctkDICOMQueryResultsTabWidget.h"
#include "ctkDICOMQueryRetrieveWidget.h"
#include "ctkDICOMQueryWidget.h"

#include "ui_ctkDICOMBrowser.h"

//logger
#include <ctkLogger.h>
static ctkLogger logger("org.commontk.DICOM.Widgets.ctkDICOMBrowser");

Q_DECLARE_METATYPE(QPersistentModelIndex);

//----------------------------------------------------------------------------
class ctkDICOMBrowserPrivate: public Ui_ctkDICOMBrowser
{
public:
  ctkDICOMBrowser* const q_ptr;
  Q_DECLARE_PUBLIC(ctkDICOMBrowser);

  ctkDICOMBrowserPrivate(ctkDICOMBrowser* );
  ~ctkDICOMBrowserPrivate();

  ctkFileDialog* ImportDialog;
  ctkDICOMQueryRetrieveWidget* QueryRetrieveWidget;

  QSharedPointer<ctkDICOMDatabase> DICOMDatabase;
  QSharedPointer<ctkDICOMThumbnailGenerator> ThumbnailGenerator;
  ctkDICOMModel DICOMModel;
  ctkDICOMFilterProxyModel DICOMProxyModel;
  QSharedPointer<ctkDICOMIndexer> DICOMIndexer;
  QProgressDialog *IndexerProgress;
  QProgressDialog *UpdateSchemaProgress;

  void showIndexerDialog();
  void showUpdateSchemaDialog();

  // used when suspending the ctkDICOMModel
  QSqlDatabase EmptyDatabase;

  QTimer* AutoPlayTimer;

  bool IsSearchWidgetPopUpMode;

  // local count variables to keep track of the number of items
  // added to the database during an import operation
  bool DisplayImportSummary;
  int PatientsAddedDuringImport;
  int StudiesAddedDuringImport;
  int SeriesAddedDuringImport;
  int InstancesAddedDuringImport;
};

//----------------------------------------------------------------------------
// ctkDICOMBrowserPrivate methods

ctkDICOMBrowserPrivate::ctkDICOMBrowserPrivate(ctkDICOMBrowser* parent): q_ptr(parent){
  DICOMDatabase = QSharedPointer<ctkDICOMDatabase> (new ctkDICOMDatabase);
  ThumbnailGenerator = QSharedPointer <ctkDICOMThumbnailGenerator> (new ctkDICOMThumbnailGenerator);
  DICOMDatabase->setThumbnailGenerator(ThumbnailGenerator.data());
  DICOMIndexer = QSharedPointer<ctkDICOMIndexer> (new ctkDICOMIndexer);
  IndexerProgress = 0;
  UpdateSchemaProgress = 0;
  DisplayImportSummary = true;
  PatientsAddedDuringImport = 0;
  StudiesAddedDuringImport = 0;
  SeriesAddedDuringImport = 0;
  InstancesAddedDuringImport = 0;
}

ctkDICOMBrowserPrivate::~ctkDICOMBrowserPrivate()
{
  if ( IndexerProgress )
    {
    delete IndexerProgress;
    }
  if ( UpdateSchemaProgress )
    {
    delete UpdateSchemaProgress;
    }
}

void ctkDICOMBrowserPrivate::showUpdateSchemaDialog()
{
  Q_Q(ctkDICOMBrowser);
  if (UpdateSchemaProgress == 0)
    {
    //
    // Set up the Update Schema Progress Dialog
    //
    UpdateSchemaProgress = new QProgressDialog(
        q->tr("DICOM Schema Update"), "Cancel", 0, 100, q,
         Qt::WindowTitleHint | Qt::WindowSystemMenuHint);

    // We don't want the progress dialog to resize itself, so we bypass the label
    // by creating our own
    QLabel* progressLabel = new QLabel(q->tr("Initialization..."));
    UpdateSchemaProgress->setLabel(progressLabel);
    UpdateSchemaProgress->setWindowModality(Qt::ApplicationModal);
    UpdateSchemaProgress->setMinimumDuration(0);
    UpdateSchemaProgress->setValue(0);

    //q->connect(UpdateSchemaProgress, SIGNAL(canceled()), 
     //       DICOMIndexer.data(), SLOT(cancel()));

    q->connect(DICOMDatabase.data(), SIGNAL(schemaUpdateStarted(int)),
            UpdateSchemaProgress, SLOT(setMaximum(int)));
    q->connect(DICOMDatabase.data(), SIGNAL(schemaUpdateProgress(int)),
            UpdateSchemaProgress, SLOT(setValue(int)));
    q->connect(DICOMDatabase.data(), SIGNAL(schemaUpdateProgress(QString)),
            progressLabel, SLOT(setText(QString)));

    // close the dialog
    q->connect(DICOMDatabase.data(), SIGNAL(schemaUpdated()),
            UpdateSchemaProgress, SLOT(close()));
    // reset the database to show new data
    q->connect(DICOMDatabase.data(), SIGNAL(schemaUpdated()),
            &DICOMModel, SLOT(reset()));
    // reset the database if canceled
    q->connect(UpdateSchemaProgress, SIGNAL(canceled()), 
            &DICOMModel, SLOT(reset()));
    }
  UpdateSchemaProgress->show();
}

void ctkDICOMBrowserPrivate::showIndexerDialog()
{
  Q_Q(ctkDICOMBrowser);
  if (IndexerProgress == 0)
    {
    //
    // Set up the Indexer Progress Dialog
    //
    IndexerProgress = new QProgressDialog( q->tr("DICOM Import"), "Cancel", 0, 100, q,
         Qt::WindowTitleHint | Qt::WindowSystemMenuHint);

    // We don't want the progress dialog to resize itself, so we bypass the label
    // by creating our own
    QLabel* progressLabel = new QLabel(q->tr("Initialization..."));
    IndexerProgress->setLabel(progressLabel);
    IndexerProgress->setWindowModality(Qt::ApplicationModal);
    IndexerProgress->setMinimumDuration(0);
    IndexerProgress->setValue(0);

    q->connect(IndexerProgress, SIGNAL(canceled()), 
                 DICOMIndexer.data(), SLOT(cancel()));

    q->connect(DICOMIndexer.data(), SIGNAL(progress(int)),
            IndexerProgress, SLOT(setValue(int)));
    q->connect(DICOMIndexer.data(), SIGNAL(indexingFilePath(QString)),
            progressLabel, SLOT(setText(QString)));
    q->connect(DICOMIndexer.data(), SIGNAL(indexingFilePath(QString)),
            q, SLOT(onFileIndexed(QString)));

    // close the dialog
    q->connect(DICOMIndexer.data(), SIGNAL(indexingComplete()),
            IndexerProgress, SLOT(close()));
    // reset the database to show new data
    q->connect(DICOMIndexer.data(), SIGNAL(indexingComplete()),
            &DICOMModel, SLOT(reset()));
    // stop indexing and reset the database if canceled
    q->connect(IndexerProgress, SIGNAL(canceled()), 
            DICOMIndexer.data(), SLOT(cancel()));
    q->connect(IndexerProgress, SIGNAL(canceled()), 
            &DICOMModel, SLOT(reset()));

    // allow users of this widget to know that the process has finished
    q->connect(IndexerProgress, SIGNAL(canceled()), 
            q, SIGNAL(directoryImported()));
    q->connect(DICOMIndexer.data(), SIGNAL(indexingComplete()),
            q, SIGNAL(directoryImported()));
    }
  IndexerProgress->show();
}

//----------------------------------------------------------------------------
// ctkDICOMBrowser methods

//----------------------------------------------------------------------------
ctkDICOMBrowser::ctkDICOMBrowser(QWidget* _parent):Superclass(_parent),
    d_ptr(new ctkDICOMBrowserPrivate(this))
{
  Q_D(ctkDICOMBrowser);

  d->setupUi(this);

  this->setSearchWidgetPopUpMode(false);

  //Hide image previewer buttons
  d->NextImageButton->hide();
  d->PrevImageButton->hide();
  d->NextSeriesButton->hide();
  d->PrevSeriesButton->hide();
  d->NextStudyButton->hide();
  d->PrevStudyButton->hide();

  //Enable sorting in tree view
  d->TreeView->setSortingEnabled(true);
  d->TreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
  d->DICOMProxyModel.setSourceModel(&d->DICOMModel);
  d->TreeView->setModel(&d->DICOMModel);

  d->ThumbnailsWidget->setThumbnailSize(
    QSize(d->ThumbnailWidthSlider->value(), d->ThumbnailWidthSlider->value()));

  // signals related to tracking inserts
  connect(d->DICOMDatabase.data(), SIGNAL(patientAdded(int,QString,QString,QString)), this,
                              SLOT(onPatientAdded(int,QString,QString,QString)));
  connect(d->DICOMDatabase.data(), SIGNAL(studyAdded(QString)), this, SLOT(onStudyAdded(QString)));
  connect(d->DICOMDatabase.data(), SIGNAL(seriesAdded(QString)), this, SLOT(onSeriesAdded(QString)));
  connect(d->DICOMDatabase.data(), SIGNAL(instanceAdded(QString)), this, SLOT(onInstanceAdded(QString)));

  // Treeview signals
  connect(d->TreeView, SIGNAL(collapsed(QModelIndex)), this, SLOT(onTreeCollapsed(QModelIndex)));
  connect(d->TreeView, SIGNAL(expanded(QModelIndex)), this, SLOT(onTreeExpanded(QModelIndex)));

  //Set ToolBar button style
  d->ToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

  //Initialize Q/R widget
  d->QueryRetrieveWidget = new ctkDICOMQueryRetrieveWidget();
  d->QueryRetrieveWidget->setWindowModality ( Qt::ApplicationModal );

  //initialize directory from settings, then listen for changes
  QSettings settings;
  if ( settings.value("DatabaseDirectory", "") == "" )
    {
    QString directory = QString("./ctkDICOM-Database");
    settings.setValue("DatabaseDirectory", directory);
    settings.sync();
    }
  QString databaseDirectory = settings.value("DatabaseDirectory").toString();
  this->setDatabaseDirectory(databaseDirectory);
  d->DirectoryButton->setDirectory(databaseDirectory);

  connect(d->DirectoryButton, SIGNAL(directoryChanged(QString)), this, SLOT(setDatabaseDirectory(QString)));

  //Initialize import widget
  d->ImportDialog = new ctkFileDialog();
  QCheckBox* importCheckbox = new QCheckBox("Copiar al importar", d->ImportDialog); //Copy on import
  //importCheckbox->setAttribute(Qt::WidgetAttribute,true);
  d->ImportDialog->setBottomWidget(importCheckbox);
  d->ImportDialog->setFileMode(QFileDialog::Directory);
  d->ImportDialog->setLabelText(QFileDialog::Accept,"Importar"); //Import
  d->ImportDialog->setWindowTitle("Importar archivos DICOM desde el directorio ..."); // Import DICOM files from directory ..."
  d->ImportDialog->setWindowModality(Qt::ApplicationModal);

  //connect signal and slots
  connect(d->TreeView, SIGNAL(clicked(QModelIndex)), d->ThumbnailsWidget, SLOT(addThumbnails(QModelIndex)));
  connect(d->TreeView, SIGNAL(clicked(QModelIndex)), d->ImagePreview, SLOT(onModelSelected(QModelIndex)));
  connect(d->TreeView, SIGNAL(clicked(QModelIndex)), this, SLOT(onModelSelected(QModelIndex)));

  connect(d->ThumbnailsWidget, SIGNAL(selected(ctkThumbnailLabel)), this, SLOT(onThumbnailSelected(ctkThumbnailLabel)));
  connect(d->ThumbnailsWidget, SIGNAL(doubleClicked(ctkThumbnailLabel)), this, SLOT(onThumbnailDoubleClicked(ctkThumbnailLabel)));
  connect(d->ImportDialog, SIGNAL(fileSelected(QString)),this,SLOT(onImportDirectory(QString)));

  connect(d->QueryRetrieveWidget, SIGNAL(canceled()), d->QueryRetrieveWidget, SLOT(hide()) );
  connect(d->QueryRetrieveWidget, SIGNAL(canceled()), this, SLOT(onQueryRetrieveFinished()) );

  connect(d->ImagePreview, SIGNAL(requestNextImage()), this, SLOT(onNextImage()));
  connect(d->ImagePreview, SIGNAL(requestPreviousImage()), this, SLOT(onPreviousImage()));
  connect(d->ImagePreview, SIGNAL(imageDisplayed(int,int)), this, SLOT(onImagePreviewDisplayed(int,int)));

  connect(d->SearchOption, SIGNAL(parameterChanged()), this, SLOT(onSearchParameterChanged()));

  connect(d->PlaySlider, SIGNAL(valueChanged(int)), d->ImagePreview, SLOT(displayImage(int)));
}

//----------------------------------------------------------------------------
ctkDICOMBrowser::~ctkDICOMBrowser()
{
  Q_D(ctkDICOMBrowser);

  d->QueryRetrieveWidget->deleteLater();
  d->ImportDialog->deleteLater();
}

//----------------------------------------------------------------------------
bool ctkDICOMBrowser::displayImportSummary()
{
  Q_D(ctkDICOMBrowser);

  return d->DisplayImportSummary;
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::setDisplayImportSummary(bool onOff)
{
  Q_D(ctkDICOMBrowser);

  d->DisplayImportSummary = onOff;
}

//----------------------------------------------------------------------------
int ctkDICOMBrowser::patientsAddedDuringImport()
{
  Q_D(ctkDICOMBrowser);

  return d->PatientsAddedDuringImport;
}

//----------------------------------------------------------------------------
int ctkDICOMBrowser::studiesAddedDuringImport()
{
  Q_D(ctkDICOMBrowser);

  return d->StudiesAddedDuringImport;
}

//----------------------------------------------------------------------------
int ctkDICOMBrowser::seriesAddedDuringImport()
{
  Q_D(ctkDICOMBrowser);

  return d->SeriesAddedDuringImport;
}

//----------------------------------------------------------------------------
int ctkDICOMBrowser::instancesAddedDuringImport()
{
  Q_D(ctkDICOMBrowser);

  return d->InstancesAddedDuringImport;
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::updateDatabaseSchemaIfNeeded()
{

  Q_D(ctkDICOMBrowser);

  d->showUpdateSchemaDialog();
  d->DICOMDatabase->updateSchemaIfNeeded();
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::setDatabaseDirectory(const QString& directory)
{
  Q_D(ctkDICOMBrowser);

  QSettings settings;
  settings.setValue("DatabaseDirectory", directory);
  settings.sync();

  //close the active DICOM database
  d->DICOMDatabase->closeDatabase();

  //open DICOM database on the directory
  QString databaseFileName = directory + QString("/ctkDICOM.sql");
  try
    {
    d->DICOMDatabase->openDatabase( databaseFileName );
    }
  catch (std::exception e)
    {
    std::cerr << "Database error: " << qPrintable(d->DICOMDatabase->lastError()) << "\n";
    d->DICOMDatabase->closeDatabase();
    return;
    }

  // update the database schema if needed and provide progress
  this->updateDatabaseSchemaIfNeeded();

  d->DICOMModel.setDatabase(d->DICOMDatabase->database());
  d->DICOMModel.setEndLevel(ctkDICOMModel::SeriesType);
  d->TreeView->resizeColumnToContents(0);

  //pass DICOM database instance to Import widget
  // d->ImportDialog->setDICOMDatabase(d->DICOMDatabase);
  d->QueryRetrieveWidget->setRetrieveDatabase(d->DICOMDatabase);

  // update the button and let any connected slots know about the change
  d->DirectoryButton->setDirectory(directory);
  d->ThumbnailsWidget->setDatabaseDirectory(directory);
  d->ImagePreview->setDatabaseDirectory(directory);
  emit databaseDirectoryChanged(directory);
}

//----------------------------------------------------------------------------
QString ctkDICOMBrowser::databaseDirectory() const
{
  QSettings settings;
  return settings.value("DatabaseDirectory").toString();
}

//----------------------------------------------------------------------------
bool ctkDICOMBrowser::searchWidgetPopUpMode(){
  Q_D(ctkDICOMBrowser);
  return d->IsSearchWidgetPopUpMode;
}

//------------------------------------------------------------------------------
void ctkDICOMBrowser::setTagsToPrecache( const QStringList tags)
{
  Q_D(ctkDICOMBrowser);
  d->DICOMDatabase->setTagsToPrecache(tags);
}

//------------------------------------------------------------------------------
const QStringList ctkDICOMBrowser::tagsToPrecache()
{
  Q_D(ctkDICOMBrowser);
  return d->DICOMDatabase->tagsToPrecache();
}



//----------------------------------------------------------------------------
ctkDICOMDatabase* ctkDICOMBrowser::database(){
  Q_D(ctkDICOMBrowser);
  return d->DICOMDatabase.data();
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::setSearchWidgetPopUpMode(bool flag){
  Q_D(ctkDICOMBrowser);

  if(flag)
    {
    d->SearchDockWidget->setTitleBarWidget(0);
    d->SearchPopUpButton->show();
    d->SearchDockWidget->hide();
    d->SearchDockWidget->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    connect(d->SearchDockWidget, SIGNAL(topLevelChanged(bool)), this, SLOT(onSearchWidgetTopLevelChanged(bool)));
    connect(d->SearchPopUpButton, SIGNAL(clicked()), this, SLOT(onSearchPopUpButtonClicked()));
    }
  else
    {
    d->SearchDockWidget->setTitleBarWidget(new QWidget());
    d->SearchPopUpButton->hide();
    d->SearchDockWidget->show();
    d->SearchDockWidget->setFeatures(QDockWidget::NoDockWidgetFeatures);
    disconnect(d->SearchDockWidget, SIGNAL(topLevelChanged(bool)), this, SLOT(onSearchWidgetTopLevelChanged(bool)));
    disconnect(d->SearchPopUpButton, SIGNAL(clicked()), this, SLOT(onSearchPopUpButtonClicked()));
    }

  d->IsSearchWidgetPopUpMode = flag;
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onFileIndexed(const QString& filePath)
{
  // Update the progress dialog when the file name changes
  // - also allows for cancel button
  QCoreApplication::instance()->processEvents();
  qDebug() << "Indexing \n\n\n\n" << filePath <<"\n\n\n";
  
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::openImportDialog()
{
  Q_D(ctkDICOMBrowser);

  d->ImportDialog->show();
  d->ImportDialog->raise();
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::openExportDialog()
{

}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::openQueryDialog()
{
  Q_D(ctkDICOMBrowser);

  d->QueryRetrieveWidget->show();
  d->QueryRetrieveWidget->raise();

}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onQueryRetrieveFinished()
{
  Q_D(ctkDICOMBrowser);
  d->DICOMModel.reset();
  emit this->queryRetrieveFinished();
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onRemoveAction()
{
  Q_D(ctkDICOMBrowser);

  //d->QueryRetrieveWidget->show();
  // d->QueryRetrieveWidget->raise();
  std::cout << "on remove" << std::endl;
  QModelIndexList selection = d->TreeView->selectionModel()->selectedIndexes();
  std::cout << selection.size() << std::endl;
  QModelIndex index;
  foreach(index,selection)
  {
    QModelIndex index0 = index.sibling(index.row(), 0);
    if ( d->DICOMModel.data(index0,ctkDICOMModel::TypeRole) == static_cast<int>(ctkDICOMModel::SeriesType))
    {
      QString seriesUID = d->DICOMModel.data(index0,ctkDICOMModel::UIDRole).toString();
      d->DICOMDatabase->removeSeries(seriesUID);
    }
    else if ( d->DICOMModel.data(index0,ctkDICOMModel::TypeRole) == static_cast<int>(ctkDICOMModel::StudyType))
    {
      QString studyUID = d->DICOMModel.data(index0,ctkDICOMModel::UIDRole).toString();
      d->DICOMDatabase->removeStudy(studyUID);
    }
    else if ( d->DICOMModel.data(index0,ctkDICOMModel::TypeRole) == static_cast<int>(ctkDICOMModel::PatientType))
    {
      QString patientUID = d->DICOMModel.data(index0,ctkDICOMModel::UIDRole).toString();
      d->DICOMDatabase->removePatient(patientUID);
    }
  }
  d->DICOMModel.reset();
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::suspendModel()
{
  Q_D(ctkDICOMBrowser);

  d->DICOMModel.setDatabase(d->EmptyDatabase);
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::resumeModel()
{
  Q_D(ctkDICOMBrowser);

  d->DICOMModel.setDatabase(d->DICOMDatabase->database());
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::resetModel()
{
  Q_D(ctkDICOMBrowser);

  d->DICOMModel.reset();
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onThumbnailSelected(const ctkThumbnailLabel& widget)
{
    Q_D(ctkDICOMBrowser);

  QModelIndex index = widget.property("sourceIndex").value<QPersistentModelIndex>();
  if(index.isValid())
    {
    d->ImagePreview->onModelSelected(index);
    }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onThumbnailDoubleClicked(const ctkThumbnailLabel& widget)
{
    Q_D(ctkDICOMBrowser);

    QModelIndex index = widget.property("sourceIndex").value<QPersistentModelIndex>();

    if(!index.isValid())
      {
      return;
      }

    ctkDICOMModel* model = const_cast<ctkDICOMModel*>(qobject_cast<const ctkDICOMModel*>(index.model()));
    QModelIndex index0 = index.sibling(index.row(), 0);

    if(model && (model->data(index0,ctkDICOMModel::TypeRole) != static_cast<int>(ctkDICOMModel::ImageType)))
      {
        this->onModelSelected(index0);
        d->TreeView->setCurrentIndex(index0);
        d->ThumbnailsWidget->addThumbnails(index0);
        d->ImagePreview->onModelSelected(index0);
      }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onPatientAdded(int databaseID, QString patientID, QString patientName, QString patientBirthDate )
{
  Q_D(ctkDICOMBrowser);
  Q_UNUSED(databaseID);
  Q_UNUSED(patientID);
  Q_UNUSED(patientName);
  Q_UNUSED(patientBirthDate);
  ++d->PatientsAddedDuringImport;
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onStudyAdded(QString studyUID)
{
  Q_D(ctkDICOMBrowser);
  Q_UNUSED(studyUID);
  ++d->StudiesAddedDuringImport;
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onSeriesAdded(QString seriesUID)
{
  Q_D(ctkDICOMBrowser);
  Q_UNUSED(seriesUID);
  ++d->SeriesAddedDuringImport;
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onInstanceAdded(QString instanceUID)
{
  Q_D(ctkDICOMBrowser);
  Q_UNUSED(instanceUID);
  ++d->InstancesAddedDuringImport;
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onImportDirectory(QString directory)
{
  Q_D(ctkDICOMBrowser);
  if (QDir(directory).exists())
    {
    QCheckBox* copyOnImport = qobject_cast<QCheckBox*>(d->ImportDialog->bottomWidget());
    QString targetDirectory;
    if (copyOnImport->checkState() == Qt::Checked)
      {
      targetDirectory = d->DICOMDatabase->databaseDirectory();
      }

    // reset counts
    d->PatientsAddedDuringImport = 0;
    d->StudiesAddedDuringImport = 0;
    d->SeriesAddedDuringImport = 0;
    d->InstancesAddedDuringImport = 0;

    // show progress dialog and perform indexing
    d->showIndexerDialog();
    d->DICOMIndexer->addDirectory(*d->DICOMDatabase,directory,targetDirectory);

    // display summary result
    if (d->DisplayImportSummary)
      {
      QString message = "Directory import completed.\n\n";
      message += QString("%1 New Patients\n").arg(QString::number(d->PatientsAddedDuringImport));
      message += QString("%1 New Studies\n").arg(QString::number(d->StudiesAddedDuringImport));
      message += QString("%1 New Series\n").arg(QString::number(d->SeriesAddedDuringImport));
      message += QString("%1 New Instances\n").arg(QString::number(d->InstancesAddedDuringImport));
      QMessageBox::information(this,"Importar directorio DICOM", message);//DICOM Directory Import
      }
  }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onModelSelected(const QModelIndex &index){
Q_D(ctkDICOMBrowser);

    ctkDICOMModel* model = const_cast<ctkDICOMModel*>(qobject_cast<const ctkDICOMModel*>(index.model()));

    if(model)
      {
        QModelIndex index0 = index.sibling(index.row(), 0);

        if ( model->data(index0,ctkDICOMModel::TypeRole) == static_cast<int>(ctkDICOMModel::PatientType) )
          {
          d->NextImageButton->show();
          d->PrevImageButton->show();
          d->NextSeriesButton->show();
          d->PrevSeriesButton->show();
          d->NextStudyButton->show();
          d->PrevStudyButton->show();
          }
        else if ( model->data(index0,ctkDICOMModel::TypeRole) == static_cast<int>(ctkDICOMModel::StudyType) )
          {
          d->NextImageButton->show();
          d->PrevImageButton->show();
          d->NextSeriesButton->show();
          d->PrevSeriesButton->show();
          d->NextStudyButton->hide();
          d->PrevStudyButton->hide();
          }
        else if ( model->data(index0,ctkDICOMModel::TypeRole) == static_cast<int>(ctkDICOMModel::SeriesType) )
          {
          d->NextImageButton->show();
          d->PrevImageButton->show();
          d->NextSeriesButton->hide();
          d->PrevSeriesButton->hide();
          d->NextStudyButton->hide();
          d->PrevStudyButton->hide();
          }
        else
          {
          d->NextImageButton->hide();
          d->PrevImageButton->hide();
          d->NextSeriesButton->hide();
          d->PrevSeriesButton->hide();
          d->NextStudyButton->hide();
          d->PrevStudyButton->hide();
          }
        d->ActionRemove->setEnabled(
            model->data(index0,ctkDICOMModel::TypeRole) == static_cast<int>(ctkDICOMModel::SeriesType) ||
            model->data(index0,ctkDICOMModel::TypeRole) == static_cast<int>(ctkDICOMModel::StudyType) ||
            model->data(index0,ctkDICOMModel::TypeRole) == static_cast<int>(ctkDICOMModel::PatientType) );
        }

      else
        {
        d->NextImageButton->hide();
        d->PrevImageButton->hide();
        d->NextSeriesButton->hide();
        d->PrevSeriesButton->hide();
        d->NextStudyButton->hide();
        d->PrevStudyButton->hide();
        d->ActionRemove->setEnabled(false);
        }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onNextImage(){
    Q_D(ctkDICOMBrowser);

    QModelIndex currentIndex = d->ImagePreview->currentImageIndex();

    if(currentIndex.isValid())
      {
      ctkDICOMModel* model = const_cast<ctkDICOMModel*>(qobject_cast<const ctkDICOMModel*>(currentIndex.model()));

      if(model)
        {
        QModelIndex seriesIndex = currentIndex.parent();

        int imageCount = model->rowCount(seriesIndex);
        int imageID = currentIndex.row();

        imageID = (imageID+1)%imageCount;

        int max = d->PlaySlider->maximum();
        if(imageID > 0 && imageID < max)
          {
            d->PlaySlider->setValue(imageID);
          }

        QModelIndex nextIndex = currentIndex.sibling(imageID, 0);

        d->ImagePreview->onModelSelected(nextIndex);
        d->ThumbnailsWidget->selectThumbnailFromIndex(nextIndex);
        }
      }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onPreviousImage(){
    Q_D(ctkDICOMBrowser);

    QModelIndex currentIndex = d->ImagePreview->currentImageIndex();

    if(currentIndex.isValid())
      {
      ctkDICOMModel* model = const_cast<ctkDICOMModel*>(qobject_cast<const ctkDICOMModel*>(currentIndex.model()));

      if(model)
        {
        QModelIndex seriesIndex = currentIndex.parent();

        int imageCount = model->rowCount(seriesIndex);
        int imageID = currentIndex.row();

        imageID--;
        if(imageID < 0) imageID += imageCount;

        int max = d->PlaySlider->maximum();
        if(imageID > 0 && imageID < max)
          {
            d->PlaySlider->setValue(imageID);
          }

        QModelIndex prevIndex = currentIndex.sibling(imageID, 0);

        d->ImagePreview->onModelSelected(prevIndex);
        d->ThumbnailsWidget->selectThumbnailFromIndex(prevIndex);
        }
      }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onNextSeries(){
    Q_D(ctkDICOMBrowser);

    QModelIndex currentIndex = d->ImagePreview->currentImageIndex();

    if(currentIndex.isValid())
      {
      ctkDICOMModel* model = const_cast<ctkDICOMModel*>(qobject_cast<const ctkDICOMModel*>(currentIndex.model()));

      if(model)
        {
        QModelIndex seriesIndex = currentIndex.parent();
        QModelIndex studyIndex = seriesIndex.parent();

        int seriesCount = model->rowCount(studyIndex);
        int seriesID = seriesIndex.row();

        seriesID = (seriesID + 1)%seriesCount;

        QModelIndex nextIndex = seriesIndex.sibling(seriesID, 0);

        d->ImagePreview->onModelSelected(nextIndex);
        d->ThumbnailsWidget->selectThumbnailFromIndex(nextIndex);
        }
      }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onPreviousSeries(){
    Q_D(ctkDICOMBrowser);

    QModelIndex currentIndex = d->ImagePreview->currentImageIndex();

    if(currentIndex.isValid())
      {
      ctkDICOMModel* model = const_cast<ctkDICOMModel*>(qobject_cast<const ctkDICOMModel*>(currentIndex.model()));

      if(model)
        {
        QModelIndex seriesIndex = currentIndex.parent();
        QModelIndex studyIndex = seriesIndex.parent();

        int seriesCount = model->rowCount(studyIndex);
        int seriesID = seriesIndex.row();

        seriesID--;
        if(seriesID < 0) seriesID += seriesCount;

        QModelIndex prevIndex = seriesIndex.sibling(seriesID, 0);

        d->ImagePreview->onModelSelected(prevIndex);
        d->ThumbnailsWidget->selectThumbnailFromIndex(prevIndex);
        }
      }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onNextStudy(){
    Q_D(ctkDICOMBrowser);

    QModelIndex currentIndex = d->ImagePreview->currentImageIndex();

    if(currentIndex.isValid())
      {
      ctkDICOMModel* model = const_cast<ctkDICOMModel*>(qobject_cast<const ctkDICOMModel*>(currentIndex.model()));

      if(model)
        {
        QModelIndex seriesIndex = currentIndex.parent();
        QModelIndex studyIndex = seriesIndex.parent();
        QModelIndex patientIndex = studyIndex.parent();

        int studyCount = model->rowCount(patientIndex);
        int studyID = studyIndex.row();

        studyID = (studyID + 1)%studyCount;

        QModelIndex nextIndex = studyIndex.sibling(studyID, 0);

        d->ImagePreview->onModelSelected(nextIndex);
        d->ThumbnailsWidget->selectThumbnailFromIndex(nextIndex);
        }
      }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onPreviousStudy(){
    Q_D(ctkDICOMBrowser);

    QModelIndex currentIndex = d->ImagePreview->currentImageIndex();

    if(currentIndex.isValid())
      {
      ctkDICOMModel* model = const_cast<ctkDICOMModel*>(qobject_cast<const ctkDICOMModel*>(currentIndex.model()));

      if(model)
        {
        QModelIndex seriesIndex = currentIndex.parent();
        QModelIndex studyIndex = seriesIndex.parent();
        QModelIndex patientIndex = studyIndex.parent();

        int studyCount = model->rowCount(patientIndex);
        int studyID = studyIndex.row();

        studyID--;
        if(studyID < 0) studyID += studyCount;

        QModelIndex prevIndex = studyIndex.sibling(studyID, 0);

        d->ImagePreview->onModelSelected(prevIndex);
        d->ThumbnailsWidget->selectThumbnailFromIndex(prevIndex);
        }
      }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onTreeCollapsed(const QModelIndex &index){
    Q_UNUSED(index);
    Q_D(ctkDICOMBrowser);
    d->TreeView->resizeColumnToContents(0);
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onTreeExpanded(const QModelIndex &index){
    Q_UNUSED(index);
    Q_D(ctkDICOMBrowser);
    d->TreeView->resizeColumnToContents(0);
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onAutoPlayCheckboxStateChanged(int state){
    Q_D(ctkDICOMBrowser);

    if(state == 0) //OFF
      {
      disconnect(d->AutoPlayTimer, SIGNAL(timeout()), this, SLOT(onAutoPlayTimer()));
      d->AutoPlayTimer->deleteLater();
      }
    else if(state == 2) //ON
      {
      d->AutoPlayTimer = new QTimer(this);
      connect(d->AutoPlayTimer, SIGNAL(timeout()), this, SLOT(onAutoPlayTimer()));
      d->AutoPlayTimer->start(50);
      }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onAutoPlayTimer(){
    this->onNextImage();
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onThumbnailWidthSliderValueChanged(int val){
  Q_D(ctkDICOMBrowser);
  d->ThumbnailsWidget->setThumbnailSize(QSize(val, val));
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onSearchParameterChanged(){
  Q_D(ctkDICOMBrowser);
  d->DICOMModel.setDatabase(d->DICOMDatabase->database(), d->SearchOption->parameters());

  this->onModelSelected(d->DICOMModel.index(0,0));
  d->ThumbnailsWidget->clearThumbnails();
  d->ThumbnailsWidget->addThumbnails(d->DICOMModel.index(0,0));
  d->ImagePreview->clearImages();
  d->ImagePreview->onModelSelected(d->DICOMModel.index(0,0));
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onImagePreviewDisplayed(int imageID, int count){
  Q_D(ctkDICOMBrowser);

  d->PlaySlider->setMinimum(0);
  d->PlaySlider->setMaximum(count-1);
  d->PlaySlider->setValue(imageID);
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onSearchPopUpButtonClicked(){
  Q_D(ctkDICOMBrowser);

  if(d->SearchDockWidget->isFloating())
    {
    d->SearchDockWidget->hide();
    d->SearchDockWidget->setFloating(false);
    }
  else
    {
    d->SearchDockWidget->setFloating(true);
    d->SearchDockWidget->adjustSize();
    d->SearchDockWidget->show();
    }
}

//----------------------------------------------------------------------------
void ctkDICOMBrowser::onSearchWidgetTopLevelChanged(bool topLevel){
  Q_D(ctkDICOMBrowser);

  if(topLevel)
    {
    d->SearchDockWidget->show();
    }
  else
    {
    d->SearchDockWidget->hide();
    }
}
