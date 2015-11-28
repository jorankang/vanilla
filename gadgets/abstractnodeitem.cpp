#include "switch.hpp"
#include "const.hpp"

#include "abstractnodeitem.hpp"

#include <QtCore>
#include <QGraphicsRectItem>
#include <QString>
#include <QLinearGradient>
#include <QPainter>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneWheelEvent>
#include <QGraphicsDropShadowEffect>
#include <QtWidgets>
#include <QtConcurrent/QtConcurrent>

#include "application.hpp"
#include "graphicstableview.hpp"

#ifdef USE_LIGHTNODE
#  include "lightnode.hpp"
#else
#  include "node.hpp"
#endif

AbstractNodeItem::AbstractNodeItem(Node *nd, int nest, QGraphicsItem *parent)
    : QGraphicsRectItem(parent)
{
    m_TableView = static_cast<GraphicsTableView*>(parent);
    m_Node = nd;
    SetNest(nest);
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    setPen(QPen(QColor(0,0,0,0)));
    setBrush(QBrush(QColor(0,0,0,0)));
    setZValue(MAIN_CONTENTS_LAYER);
}

AbstractNodeItem::~AbstractNodeItem(){
}

Node *AbstractNodeItem::GetNode(){
    return m_Node;
}

GraphicsTableView *AbstractNodeItem::GetTableView(){
    return m_TableView;
}

void AbstractNodeItem::Initialize(){
    // settings
}

void AbstractNodeItem::SetNest(int nest){
    m_NestLevel = nest;
}

int AbstractNodeItem::GetNest(){
    return m_NestLevel;
}

bool AbstractNodeItem::IsPrimary(){
    return false;
}

bool AbstractNodeItem::IsHovered(){
    return false;
}

void AbstractNodeItem::SetPrimary(){
}

void AbstractNodeItem::SetHovered(){
}

void AbstractNodeItem::SetSelectionRange(){
}

void AbstractNodeItem::ClearOtherSectionSelection(){
}

void AbstractNodeItem::ApplyChildrenOrder(QPointF pos){
    Q_UNUSED(pos);
}

QVariant AbstractNodeItem::itemChange(GraphicsItemChange change, const QVariant &value){
    if(change == ItemSelectedChange && scene()){

        // needless to call 'GraphicsTableView::AppendToSelection'
        // or 'GraphicsTableView::RemoveFromSelection',
        // because want to save selection, even if moving directory.

        if(value.toBool()){
            setZValue(DRAGGING_CONTENTS_LAYER);
        } else {
            setZValue(MAIN_CONTENTS_LAYER);
        }
    }
    return QGraphicsRectItem::itemChange(change, value);
}

void AbstractNodeItem::dragEnterEvent(QGraphicsSceneDragDropEvent *ev){
    QGraphicsRectItem::dragEnterEvent(ev);
}

void AbstractNodeItem::dropEvent(QGraphicsSceneDragDropEvent *ev){
    QGraphicsRectItem::dropEvent(ev);
}

void AbstractNodeItem::dragMoveEvent(QGraphicsSceneDragDropEvent *ev){
    QGraphicsRectItem::dragMoveEvent(ev);
}

void AbstractNodeItem::dragLeaveEvent(QGraphicsSceneDragDropEvent *ev){
    QGraphicsRectItem::dragLeaveEvent(ev);
}

void AbstractNodeItem::mousePressEvent(QGraphicsSceneMouseEvent *ev){
    m_TableView->SetInPlaceNotifierContent(0);

    if(ev->button() == Qt::RightButton){
        // for sort menu.
        ev->setAccepted(!(Application::keyboardModifiers() & Qt::ControlModifier));
        return;
    }

    if(ev->button() != Qt::LeftButton){
        // call GraphicsTableView's mouse event.
        QGraphicsRectItem::mousePressEvent(ev);
        return;
    }

    if(Application::keyboardModifiers() & Qt::ControlModifier){
        setSelected(!isSelected());
        if(isSelected()){
            m_TableView->AppendToSelection(m_Node);
        } else {
            m_TableView->RemoveFromSelection(m_Node);
        }
    } else if(Application::keyboardModifiers() & Qt::ShiftModifier){
        SetSelectionRange();
    } else {
        ClearOtherSectionSelection();
        m_TableView->ClearScrollControllerSelection();
        QGraphicsRectItem::mousePressEvent(ev);
    }
    ev->setAccepted(true);
}

void AbstractNodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *ev){
    if(ev->button() == Qt::RightButton){

        if(GraphicsTableView::RightClickToRenameNode() &&
           m_Node->TitleEditable()){

            ev->setAccepted(m_TableView->ThumbList_RenameNode());

        } else if(QMenu *menu = m_TableView->CreateNodeMenu()){

            menu->exec(ev->screenPos());
            delete menu;
            ev->setAccepted(true);
        }
        return;
    }
    if(ev->button() == Qt::MidButton){
        ev->setAccepted(m_TableView->ThumbList_DeleteNode());
        return;
    }
    if(ev->button() != Qt::LeftButton){
        return;
    }
    if(pos().manhattanLength() > Application::startDragDistance()){

        // needless to call 'update',
        // because
        // 'ApplyChildrenOrder' calls 'SetCurrent'.
        // 'SetCurrent' calls 'CollectNodes'.
        // 'CollectNodes' calls 'SetScrollSoft'.
        // 'SetScrollSoft' calls 'SetScroll'.
        // 'SetScroll' calls 'update()'.

        ApplyChildrenOrder(ev->scenePos());
        ev->setAccepted(true);

    } else if(pos().manhattanLength() != 0){
        foreach(QGraphicsItem *item, scene()->selectedItems()){ item->setPos(QPoint(0,0));}
    } else if(Application::keyboardModifiers() & Qt::ControlModifier){
        /* do nothing.*/
    } else if(Application::keyboardModifiers() & Qt::ShiftModifier){
        /* do nothing.*/
    } else {
        m_TableView->ThumbList_OpenNode();
        ev->setAccepted(true);
    }
}

void AbstractNodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent *ev){
    if(ev->buttons() & Qt::RightButton){
        ev->setAccepted(false); return;
    }

    ClearOtherSectionSelection();
    m_TableView->ClearScrollControllerSelection();

    QList<QRectF> list;

    if(m_TableView->GetHoveredSpotLight()){
        list << m_TableView->GetHoveredSpotLight()->boundingRect();
    }

    if(m_TableView->GetPrimarySpotLight() &&
       m_TableView->GetHoveredItemIndex() != m_TableView->GetPrimaryItemIndex() &&
       ((m_TableView->GetPrimaryThumbnail() &&
         m_TableView->GetPrimaryThumbnail()->isSelected()) ||
        (m_TableView->GetPrimaryNodeTitle() &&
         m_TableView->GetPrimaryNodeTitle()->isSelected()))){
        list << m_TableView->GetPrimarySpotLight()->boundingRect();
    }

    foreach(SpotLight *light, m_TableView->GetLoadedSpotLights()){
        if(scene()->selectedItems().contains(light)){
            list << light->boundingRect();
        }
    }

    foreach(QGraphicsItem *item, scene()->selectedItems()){
        list << item->boundingRect().translated(item->pos());
    }

    QGraphicsRectItem::mouseMoveEvent(ev);

    if(m_TableView->GetHoveredSpotLight()){
        list << m_TableView->GetHoveredSpotLight()->boundingRect();
    }

    if(m_TableView->GetPrimarySpotLight() &&
       m_TableView->GetHoveredItemIndex() != m_TableView->GetPrimaryItemIndex() &&
       ((m_TableView->GetPrimaryThumbnail() &&
         m_TableView->GetPrimaryThumbnail()->isSelected()) ||
        (m_TableView->GetPrimaryNodeTitle() &&
         m_TableView->GetPrimaryNodeTitle()->isSelected()))){
        list << m_TableView->GetPrimarySpotLight()->boundingRect();
    }

    foreach(SpotLight *light, m_TableView->GetLoadedSpotLights()){
        if(scene()->selectedItems().contains(light)){
            list << light->boundingRect();
        }
    }

    foreach(QGraphicsItem *item, scene()->selectedItems()){
        list << item->boundingRect().translated(item->pos());
    }

    foreach(QRectF rect, list){
        m_TableView->Update(rect);
    }

    ev->setAccepted(true);
}

void AbstractNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev){
    QGraphicsRectItem::mouseDoubleClickEvent(ev);
}

void AbstractNodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent *ev){
    QGraphicsRectItem::hoverEnterEvent(ev);
    SetHovered();
    m_TableView->SetInPlaceNotifierContent(m_Node);
    m_TableView->SetInPlaceNotifierPosition(ev->scenePos());
}

void AbstractNodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *ev){
    QGraphicsRectItem::hoverLeaveEvent(ev);
}

void AbstractNodeItem::hoverMoveEvent(QGraphicsSceneHoverEvent *ev){
    QGraphicsRectItem::hoverMoveEvent(ev);
    SetHovered();
    m_TableView->SetInPlaceNotifierContent(m_Node);
    m_TableView->SetInPlaceNotifierPosition(ev->scenePos());
}

void AbstractNodeItem::wheelEvent(QGraphicsSceneWheelEvent *ev){
    // call GraphicsTableView's wheel event.
    QGraphicsRectItem::wheelEvent(ev);
}