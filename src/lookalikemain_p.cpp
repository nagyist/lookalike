#include "facedatabaseprovider.h"
#include "facetrackerprovider.h"
#include "galleryfullscreenpage.h"
#include "gallerygridpage.h"
#include "gallerymodel.h"
#include "gallerypeoplelistpage.h"
#include "lookalikemain_p.h"
#include "math.h"
#include "trackercontentprovider.h"
#include <MAction>
#include <MApplication>
#include <MApplicationPage>
#include <MApplicationWindow>
#include <MDialog>
#include <MImageWidget>
#include <MProgressIndicator>
#include <MSceneManager>
#include <MToolBar>
#include <MWidgetAction>
#include <QAbstractItemModel>
#include <QSparqlConnection>
#include <QTimer>
#include <QuillMetadata>
#include <QuillMetadataRegion>
#include <QuillMetadataRegionList>
#include <xqfacedatabase.h>

LookAlikeMainPrivate::LookAlikeMainPrivate(LookAlikeMain *q) :
    QObject(q),
    q_ptr(q)
{
    m_galleryModel = new GalleryModel(this);
    m_galleryModel->setFaceRecognitionEnabled(true);
    QSparqlConnection *connection = m_galleryModel->sparqlConnection();
    m_trackerProvider = new TrackerContentProvider(connection, this);
    m_galleryModel->addContentProvider(m_trackerProvider);
    m_faceDatabaseProvider = new FaceDatabaseProvider(connection, this);
    m_faceTrackerProvider = new FaceTrackerProvider(connection, this);
    m_confirmedPeopleListPage = new GalleryPeopleListPage(m_faceTrackerProvider->model());
    m_unconfirmedPeopleListPage = new GalleryPeopleListPage(m_faceDatabaseProvider);
    m_gridPage = new GalleryGridPage(*m_galleryModel);
    m_fullScreenPage = new GalleryFullScreenPage(*m_galleryModel);

    m_confirmedPeopleListPage->setStyleName("GalleryPage");
    m_unconfirmedPeopleListPage->setStyleName("GalleryPage");
    m_gridPage->setStyleName("GalleryPage");
    m_fullScreenPage->setStyleName("GalleryPage");

    MAction* allTabAction = new MAction("icon-m-toolbar-all-content-white", "", q);
    allTabAction->setLocation(MAction::ToolBarLocation);
    allTabAction->setCheckable(true);
    allTabAction->setToggledIconID("icon-m-toolbar-all-content-selected");

    MAction* confirmedPeopleTabAction = new MAction("icon-m-toolbar-people-white", "", q);
    confirmedPeopleTabAction->setLocation(MAction::ToolBarLocation);
    confirmedPeopleTabAction->setCheckable(true);
    confirmedPeopleTabAction->setToggledIconID("icon-m-toolbar-people-selected");

    MAction* unconfirmedPeopleTabAction = new MAction("icon-m-toolbar-addressbook-white", "", q);
    unconfirmedPeopleTabAction->setLocation(MAction::ToolBarLocation);
    unconfirmedPeopleTabAction->setCheckable(true);
    unconfirmedPeopleTabAction->setToggledIconID("icon-m-toolbar-addressbook-selected");

    QList<QAction*> actions;
    actions.append(allTabAction);
    actions.append(confirmedPeopleTabAction);
    actions.append(unconfirmedPeopleTabAction);

    MToolBar* toolbar = new MToolBar();
    toolbar->setStyleName("MToolbarTabStyleInverted");
    toolbar->setOpacity(0.9);
    toolbar->setViewType(MToolBar::tabType);
    toolbar->addActions(actions);

    m_toolbarAction = new MWidgetAction(q);
    m_toolbarAction->setLocation(MAction::ToolBarLocation);
    m_toolbarAction->setWidget(toolbar);

    m_confirmFaceAction = new MAction("Confirm faces", q);
    m_confirmFaceAction->setLocation(MAction::ApplicationMenuLocation);

    const QPixmap *pixmap = MTheme::pixmap("icon-m-toolbar-view-menu-dimmed-white");
    MImageWidget *menuImage = new MImageWidget();
    menuImage->setImage(pixmap->toImage());
    menuImage->setZoomFactor(1.f);
    menuImage->setMinimumWidth(88.f);
    MTheme::releasePixmap(pixmap);
    MWidgetAction* fakeAction= new MWidgetAction(q);
    fakeAction->setLocation(MAction::ToolBarLocation);
    fakeAction->setWidget(menuImage);

    m_confirmedPeopleListPage->addAction(m_toolbarAction);
    m_confirmedPeopleListPage->addAction(fakeAction);

    m_unconfirmedPeopleListPage->addAction(m_toolbarAction);
    m_unconfirmedPeopleListPage->addAction(fakeAction);

    connect(m_confirmedPeopleListPage, SIGNAL(personSelected(QString,QString)),
            this, SLOT(onConfirmedPersonSelected(QString,QString)));
    connect(m_unconfirmedPeopleListPage, SIGNAL(personSelected(QString,QString)),
            this, SLOT(onUnconfirmedPersonSelected(QString,QString)));
    connect(m_gridPage, SIGNAL(multiSelectionDone(QList<QUrl>)),
            this, SLOT(onMultiSelectionDone(QList<QUrl>)));
    connect(m_gridPage, SIGNAL(itemSelected(QUrl)),
            this, SLOT(onItemSelected(QUrl)));
    connect(m_fullScreenPage, SIGNAL(itemDeleted(QUrl)),
            m_faceDatabaseProvider, SLOT(update()));
    connect(m_trackerProvider, SIGNAL(dataChanged()),
            this, SLOT(onDataChanged()));
    connect(m_confirmFaceAction, SIGNAL(triggered()),
            this, SLOT(onConfirmFaceActionTriggered()));
    connect(allTabAction, SIGNAL(toggled(bool)),
            this, SLOT(onAllTabActionToggled(bool)));
    connect(confirmedPeopleTabAction, SIGNAL(toggled(bool)),
            this, SLOT(onConfirmedPeopleTabActionToggled(bool)));
    connect(unconfirmedPeopleTabAction, SIGNAL(toggled(bool)),
            this, SLOT(onUnconfirmedPeopleTabActionToggled(bool)));

    allTabAction->toggle();
}

LookAlikeMainPrivate::~LookAlikeMainPrivate()
{
    m_galleryModel->removeContentProvider(m_trackerProvider);
    delete m_unconfirmedPeopleListPage;
    delete m_confirmedPeopleListPage;
    delete m_gridPage;
}

QString LookAlikeMainPrivate::urnFromUrl(QUrl url)
{
    QAbstractItemModel *model = m_trackerProvider->model();
    QString urlString = url.toString();
    for (int i = 0; i < model->rowCount(); i++) {
        if (model->index(i, GalleryContentProvider::UrlColumn).data().toString() == urlString) {
            return model->index(i, GalleryContentProvider::UrnColumn).data().toString();
        }
    }
    return QString();
}

XQFaceRegion LookAlikeMainPrivate::findRegion(QString &faceId, QString &sourceId)
{
    QList<XQFaceRegion> regions = m_faceDatabaseProvider->getRegions(faceId);

    foreach (XQFaceRegion r, regions) {
        if (r.sourceId() == sourceId) {
            return r;
        }
    }

    //We shouldn't reach this point
    Q_ASSERT(false);
    return regions.at(0);
}

QRect LookAlikeMainPrivate::scaleRect(const QRect &rect, QSize &fromSize, QSize &toSize)
{
    if (fromSize == toSize) {
        return rect;
    }

    qreal xFactor = fabs((qreal) toSize.width() / (qreal) fromSize.width());
    qreal yFactor = fabs((qreal) toSize.height() / (qreal) fromSize.height());
    int x = xFactor * rect.topLeft().x();
    int y = yFactor * rect.topLeft().y();
    int width = xFactor * rect.width();
    int height = yFactor * rect.height();

    return QRect(x, y, width, height);
}

void LookAlikeMainPrivate::updateTrackerFilter()
{
    m_galleryModel->removeContentProvider(m_trackerProvider);
    QSet<QString> urnImages;
    QList<XQFaceRegion> regions = m_faceDatabaseProvider->getRegions(m_personSelected);
    foreach(XQFaceRegion region, regions) {
        urnImages << region.sourceId();
    }
    m_trackerProvider->setUrns(urnImages);
    m_galleryModel->addContentProvider(m_trackerProvider);
}

void LookAlikeMainPrivate::updateGrid()
{
    m_gridPage->showTopBar(false);
    m_gridPage->removeAction(m_confirmFaceAction);
    m_gridPage->addAction(m_toolbarAction);
    m_gridPage->resetToDefaultState();
}

void LookAlikeMainPrivate::updateGrid(const QString &displayName, bool addConfirmationMenu)
{
    m_gridPage->setTopBarText(displayName);
    m_gridPage->showTopBar(true);
    m_gridPage->removeAction(m_toolbarAction);
    //Remove and insert again to ensure it is always the first action
    m_gridPage->removeAction(m_confirmFaceAction);
    if (addConfirmationMenu) {
        if (m_gridPage->actions().isEmpty()) {
            m_gridPage->addAction(m_confirmFaceAction);
        } else {
            m_gridPage->insertAction(m_gridPage->actions().first(), m_confirmFaceAction);
        }
    }
    m_gridPage->resetToDefaultState();
}

void LookAlikeMainPrivate::showPage(MApplicationPage *page, bool history)
{
    MApplication::activeApplicationWindow()->sceneManager()->appearSceneWindowNow(page);
    if (!history) {
        MApplication::activeWindow()->sceneManager()->setPageHistory(QList<MSceneWindow*>());
    }
}

void LookAlikeMainPrivate::confirmFace(QUrl picture, QString& contact)
{
    //Confirm in Face Recognizer database
    QString urnImage = urnFromUrl(picture);
    XQFaceRegion region = findRegion(contact, urnImage);
    region.setFaceId(contact);

    //Update XMP metadata image
    QString fileName = picture.toLocalFile();
    QImage image(fileName);
    //Scale face to image dimensions
    QSize originalSize = image.size();
    QSize thumbnailSize = region.sourceImageSize();
    QRect rect = region.faceRect();
    QRect scaledRect = scaleRect(rect, thumbnailSize, originalSize);
    QuillMetadataRegion metadataRegion;
    metadataRegion.setArea(scaledRect);
    metadataRegion.setType(QuillMetadataRegion::RegionType_Face);
    QString contactName = m_faceDatabaseProvider->getContactName(contact);
    metadataRegion.setName(contactName);
    metadataRegion.setExtension("nco:PersonContact", contact);
    QuillMetadataRegionList regionList;
    regionList.append(metadataRegion);
    QuillMetadata metadata(fileName, QuillMetadata::XmpFormat);
    QVariant variant;
    variant.setValue(regionList);
    metadata.setEntry(QuillMetadata::Tag_Regions, variant);
    metadata.write(fileName, QuillMetadata::XmpFormat);

}

void LookAlikeMainPrivate::onUnconfirmedPersonSelected(const QString &personId, const QString &displayName)
{
    m_personSelected = personId;
    if (m_personSelected == UNKNOWN_CONTACT) {
        updateGrid(displayName, false);
    } else {
        updateGrid(displayName, true);
    }
    m_trackerProvider->setContentType(TrackerContentProvider::ListImages);
    updateTrackerFilter();
    showPage(m_gridPage, true);
}

void LookAlikeMainPrivate::onConfirmedPersonSelected(const QString &personId, const QString &displayName)
{
    updateGrid(displayName, false);
    m_trackerProvider->setContact(personId);
    m_trackerProvider->setContentType(TrackerContentProvider::WithContact);
    updateTrackerFilter();
    showPage(m_gridPage, true);
}

void LookAlikeMainPrivate::onConfirmFaceActionTriggered()
{
    m_gridPage->startMultiSelection("Confirm");
}

void LookAlikeMainPrivate::onMultiSelectionDone(QList<QUrl> urlList)
{
    m_facesToConfirm = urlList;
    m_progress = new MProgressIndicator(0, MProgressIndicator::barType);
    m_progress->setStyleName(("CommonProgressBarInverted"));
    m_progress->setRange(0, urlList.size());
    m_progressDialog = new MDialog();
    m_progressDialog->setCentralWidget(m_progress);
    m_progressDialog->appear(MSceneWindow::DestroyWhenDone);

    connect(m_progressDialog, SIGNAL(appeared()),
            this, SLOT(confirmFaces()),
            Qt::QueuedConnection);
    connect(m_progressDialog, SIGNAL(rejected()),
            this, SLOT(onProgressDialogRejected()));
    connect(m_progress, SIGNAL(valueChanged(int)),
            this, SLOT(confirmFaces()),
            Qt::QueuedConnection);
}

void LookAlikeMainPrivate::confirmFaces()
{
    if (m_facesToConfirm.isEmpty()) {
        m_progressDialog->dismiss();
    } else {
        QUrl toConfirm = m_facesToConfirm.first();
        confirmFace(toConfirm, m_personSelected);
        m_facesToConfirm.removeFirst();
        m_progress->setValue(m_progress->value() + 1);
    }
}

void LookAlikeMainPrivate::onItemSelected(const QUrl &url)
{
    m_fullScreenPage->resetToDefaultState();
    m_fullScreenPage->moveToItem(url);
    m_fullScreenPage->appear(MApplication::activeWindow());
    m_fullScreenPage->setNavigationBarVisible(false);
}

void LookAlikeMainPrivate::onDataChanged()
{
    m_faceDatabaseProvider->update();
    updateTrackerFilter();
}

void LookAlikeMainPrivate::onAllTabActionToggled(bool toggled)
{
    if (toggled) {
        m_personSelected = UNKNOWN_CONTACT;
        updateGrid();
        m_trackerProvider->setContentType(TrackerContentProvider::AllImages);
        updateTrackerFilter();
        showPage(m_gridPage);
    }
}

void LookAlikeMainPrivate::onUnconfirmedPeopleTabActionToggled(bool toggled)
{
    if (toggled) {
        m_personSelected = UNKNOWN_CONTACT;
        showPage(m_unconfirmedPeopleListPage);
    }
}

void LookAlikeMainPrivate::onConfirmedPeopleTabActionToggled(bool toggled)
{
    if (toggled) {
        m_personSelected = UNKNOWN_CONTACT;
        showPage(m_confirmedPeopleListPage);
    }
}

void LookAlikeMainPrivate::onProgressDialogRejected()
{
    m_facesToConfirm.clear();
}
