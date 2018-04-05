#include "switch.hpp"
#include "const.hpp"

#include "treebar.hpp"

#include "treebank.hpp"
#include "gadgets.hpp"
#include "webengineview.hpp"

#include <QPainter>
#include <QGraphicsScene>
#include <QGraphicsObject>
#include <QPropertyAnimation>
#include <QWidgetAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QStyle>
#include <QStyleOptionToolBar>

/*
  collecting model.
    horizontal(like Tungsten):
      * all siblings of all ancestors of current view node.
      * grid layout.
      * movable(to other directory also).
      * scroll siblings line, when mouse cursor is out of widget.
    vertical(like 'Tree Style Tab' of Firefox):
      * collect siblings of current view node,
        and collect parent's siblings if not that is not folded,
        repeat above.
      * movable(to other directory also).
      * foldable.
      * scroll siblings line, when mouse cursor is out of widget.

  ZValue
   0.0 : layer item layer.
  10.0 : normal node item layer.
  15.0 : border line layer.
  20.0 : primary node item layer.
  30.0 : dragging node layer.
  40.0 : fringe button layer.

 */

#define LAYER_ITEM_LAYER 0.0

#define DELETED_NODE_LAYER 5.0
#define NORMAL_NODE_LAYER 10.0
#define BORDEF_LINE_LAYER 15.0
#define FOCUSED_NODE_LAYER 20.0
#define DRAGGING_NODE_LAYER 30.0
#define FRINGE_BUTTON_LAYER 40.0
#define SCROLL_INDICATOR_LAYER 50.0

#define FRINGE_BUTTON_SIZE 19

#define SCROLL_INDICATOR_SIZE 35

#define TREEBAR_HORIZONTAL_DEFAULT_WIDTH 300
#define TREEBAR_VERTICAL_DEFAULT_HEIGHT 500

#define TREEBAR_HORIZONTAL_NODE_DEFAULT_WIDTH 150
#define TREEBAR_HORIZONTAL_NODE_DEFAULT_HEIGHT 24

#define TREEBAR_HORIZONTAL_NODE_MINIMUM_HEIGHT 24
#define TREEBAR_HORIZONTAL_NODE_MAXIMUM_HEIGHT 135
#define TREEBAR_HORIZONTAL_NODE_MINIMUM_WIDTH 50
#define TREEBAR_HORIZONTAL_NODE_MAXIMUM_WIDTH 300

#define TREEBAR_VERTICAL_NODE_DEFAULT_WIDTH 250
#define TREEBAR_VERTICAL_NODE_DEFAULT_HEIGHT 24

#define TREEBAR_VERTICAL_NODE_MINIMUM_WIDTH 70 /*
#define TREEBAR_VERTICAL_NODE_MAXIMUM_WIDTH unlimited */
#define TREEBAR_VERTICAL_NODE_MINIMUM_HEIGHT 24
#define TREEBAR_VERTICAL_NODE_MAXIMUM_HEIGHT 135

bool TreeBar::m_EnableAnimation    = false;
bool TreeBar::m_EnableCloseButton  = false;
bool TreeBar::m_EnableCloneButton  = false;
bool TreeBar::m_ScrollToSwitchNode = false;
bool TreeBar::m_WheelClickToClose  = false;

namespace {

    template <class T>
    bool Cramp(T &value, const T min, const T max){
        if(max < min) return false;
        if(value > max) value = max;
        if(value < min) value = min;
        return true;
    }

    class ResizeGrip : public QWidget {
    public:
        ResizeGrip(QWidget *parent = 0)
            : QWidget(parent)
        {
        }
    protected:
        void paintEvent(QPaintEvent *ev) Q_DECL_OVERRIDE {
            Q_UNUSED(ev);
            QPainter painter(this);
            static const QBrush b = QBrush(QColor(240, 240, 240, 1));
            painter.setPen(Qt::NoPen);
            painter.setBrush(b);
            painter.drawRect(-1, -1, width()+1, height()+1);
        }
        void enterEvent(QEvent *ev) Q_DECL_OVERRIDE {
            QWidget::enterEvent(ev);
            TreeBar *bar = static_cast<TreeBar*>(parentWidget());
            switch(bar->orientation()){
            case Qt::Horizontal: setCursor(Qt::SizeVerCursor); break;
            case Qt::Vertical:   setCursor(Qt::SizeHorCursor); break;
            }
        }
        void leaveEvent(QEvent *ev) Q_DECL_OVERRIDE {
            QWidget::leaveEvent(ev);
            setCursor(Qt::ArrowCursor);
        }
        void mouseMoveEvent(QMouseEvent *ev) Q_DECL_OVERRIDE {
            QWidget::mouseMoveEvent(ev);

            TreeBar *bar = static_cast<TreeBar*>(parentWidget());
            MainWindow *win = static_cast<MainWindow*>(bar->parentWidget());

            int width  = bar->width();
            int height = bar->height();

            switch(win->toolBarArea(bar)){
            case Qt::TopToolBarArea:    height =          mapToParent(ev->pos()).y(); break;
            case Qt::BottomToolBarArea: height = height - mapToParent(ev->pos()).y(); break;
            case Qt::LeftToolBarArea:   width  =          mapToParent(ev->pos()).x(); break;
            case Qt::RightToolBarArea:  width  = width  - mapToParent(ev->pos()).x(); break;
            default: break;
            }
            switch(bar->orientation()){
            case Qt::Horizontal:
                Cramp(height, bar->MinHeight(), bar->MaxHeight());
                setCursor(Qt::SizeVerCursor);
                break;
            case Qt::Vertical:
                Cramp(width, bar->MinWidth(), bar->MaxWidth());
                setCursor(Qt::SizeHorCursor);
                break;
            }
            bar->resize(width, height);
            bar->Adjust();
        }
        void mousePressEvent(QMouseEvent *ev) Q_DECL_OVERRIDE {
            QWidget::mousePressEvent(ev);
        }
        void mouseReleaseEvent(QMouseEvent *ev) Q_DECL_OVERRIDE {
            QWidget::mouseReleaseEvent(ev);
        }
        void mouseDoubleClickEvent(QMouseEvent *ev) Q_DECL_OVERRIDE {
            QWidget::mouseDoubleClickEvent(ev);

            TreeBar *bar = static_cast<TreeBar*>(parentWidget());

            switch(bar->orientation()){

            case Qt::Horizontal:
                if(bar->height() == bar->MinHeight())
                    bar->resize(bar->width(), bar->MaxHeight());
                else
                    bar->resize(bar->width(), bar->MinHeight());
                break;
            case Qt::Vertical:
                if(bar->width() == bar->MinWidth())
                    bar->resize(bar->MaxWidth(), bar->height());
                else
                    bar->resize(bar->MinWidth(), bar->height());
                break;
            }
            bar->Adjust();
        }
    };

    class GraphicsButton : public QGraphicsItem {
    public:
        GraphicsButton(TreeBank *tb, TreeBar *bar, QGraphicsItem *parent = 0)
            : QGraphicsItem(parent)
        {
            m_TreeBank = tb;
            m_TreeBar = bar;
            m_ButtonState = NotHovered;
            setZValue(FRINGE_BUTTON_LAYER);
            setAcceptHoverEvents(true);
        }

        enum ButtonState{
            NotHovered,
            Hovered,
            Pressed,
        } m_ButtonState;

        void SetButtonState(ButtonState state){
            if(m_ButtonState != state){
                m_ButtonState = state;
                update();
            }
        }

    protected:
        virtual void mousePressEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            Q_UNUSED(ev);
            SetButtonState(Pressed);
        }
        virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            if(boundingRect().contains(ev->pos()))
                SetButtonState(Hovered);
            else SetButtonState(NotHovered);
        }
        virtual void mouseMoveEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            QGraphicsItem::mouseMoveEvent(ev);
            SetButtonState(Hovered);
        }
        virtual void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            QGraphicsItem::mouseDoubleClickEvent(ev);
        }
        virtual void hoverEnterEvent(QGraphicsSceneHoverEvent *ev) Q_DECL_OVERRIDE {
            QGraphicsItem::hoverEnterEvent(ev);
            SetButtonState(Hovered);
        }
        virtual void hoverLeaveEvent(QGraphicsSceneHoverEvent *ev) Q_DECL_OVERRIDE {
            QGraphicsItem::hoverLeaveEvent(ev);
            SetButtonState(NotHovered);
        }
        virtual void hoverMoveEvent(QGraphicsSceneHoverEvent *ev) Q_DECL_OVERRIDE {
            QGraphicsItem::hoverMoveEvent(ev);
            SetButtonState(Hovered);
        }

        TreeBank *m_TreeBank;
        TreeBar *m_TreeBar;
    };

    class TableButton : public GraphicsButton {
    public:
        TableButton(TreeBank *tb, TreeBar *bar, QGraphicsItem *parent = 0)
            : GraphicsButton(tb, bar, parent)
        {
        }
        QRectF boundingRect() const Q_DECL_OVERRIDE {
            QRectF rect = scene()->sceneRect();
            switch(m_TreeBar->orientation()){
            case Qt::Horizontal: rect.setWidth(17);  break;
            case Qt::Vertical:   rect.setHeight(17); break;
            }
            return rect;
        }

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) Q_DECL_OVERRIDE {
            Q_UNUSED(option); Q_UNUSED(widget);

            painter->save();

            if(Application::EnableTransparentBar()){
                QPainter::CompositionMode mode = painter->compositionMode();
                painter->setCompositionMode(QPainter::CompositionMode_Clear);
                painter->fillRect(boundingRect(), Qt::BrushStyle::SolidPattern);
                painter->setCompositionMode(mode);

                const QBrush brush(QColor(0, 0, 0, 1));
                painter->setBrush(brush);
            } else {
                const QBrush brush(QColor(240, 240, 240, 255));
                painter->setBrush(brush);
            }
            painter->setPen(Qt::NoPen);
            painter->drawRect(boundingRect());

            if(m_ButtonState == Hovered || m_ButtonState == Pressed){
                static const QBrush h = QBrush(QColor(180, 180, 180, 255));
                static const QBrush p = QBrush(QColor(150, 150, 150, 255));
                painter->setBrush(m_ButtonState == Hovered ? h : p);
                painter->setPen(Qt::NoPen);
                painter->setRenderHint(QPainter::Antialiasing, true);
                QPoint offset;
                switch(m_TreeBar->orientation()){
                case Qt::Horizontal: offset = QPoint(-9, -8); break;
                case Qt::Vertical:   offset = QPoint(-8, -9); break;
                }
                painter->drawRoundedRect(QRect(boundingRect().center().toPoint() + offset,
                                               QSize(13, 13)), 1, 1);
            }
            static QPixmap table = QPixmap(":/resources/treebar/table.png");
            QPoint offset;
            switch(m_TreeBar->orientation()){
            case Qt::Horizontal: offset = QPoint(-8, -7); break;
            case Qt::Vertical:   offset = QPoint(-7, -8); break;
            }
            painter->drawPixmap
                (QRect(boundingRect().center().toPoint() + offset,
                       table.size()),
                 table, QRect(QPoint(), table.size()));

            painter->restore();
        }

    protected:
        void mousePressEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mousePressEvent(ev);
        }
        void mouseReleaseEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mouseReleaseEvent(ev);

            if(ev->button() == Qt::LeftButton){
                if((ev->buttonDownScreenPos(Qt::LeftButton) - ev->screenPos()).manhattanLength() < 4){
                    Gadgets *g = m_TreeBank->GetGadgets();
                    if(g && g->IsActive()) g->Deactivate();
                    else m_TreeBank->DisplayViewTree();
                }
            } else if(ev->button() == Qt::RightButton){
                if((ev->buttonDownScreenPos(Qt::RightButton) - ev->screenPos()).manhattanLength() < 4){
                    QMenu *menu = m_TreeBar->TreeBarMenu();
                    menu->exec(ev->screenPos());
                    delete menu;
                }
            }
        }
    };

    class PlusButton : public GraphicsButton {
    public:
        PlusButton(TreeBank *tb, TreeBar *bar, QGraphicsItem *parent = 0)
            : GraphicsButton(tb, bar, parent)
        {
        }
        QRectF boundingRect() const Q_DECL_OVERRIDE {
            QRectF rect = parentItem()->boundingRect();
            switch(m_TreeBar->orientation()){
            case Qt::Horizontal: rect.setLeft(rect.right() - 17); break;
            case Qt::Vertical:   rect.setTop(rect.bottom() - 17); break;
            }
            return rect;
        }

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) Q_DECL_OVERRIDE {
            Q_UNUSED(option); Q_UNUSED(widget);

            if(Application::EnableTransparentBar()){
                QPainter::CompositionMode mode = painter->compositionMode();
                painter->setCompositionMode(QPainter::CompositionMode_Clear);
                painter->fillRect(boundingRect(), Qt::BrushStyle::SolidPattern);
                painter->setCompositionMode(mode);

                const QBrush brush(QColor(0, 0, 0, 1));
                painter->setBrush(brush);
            } else {
                const QBrush brush(QColor(240, 240, 240, 255));
                painter->setBrush(brush);
            }
            painter->setPen(Qt::NoPen);
            painter->drawRect(boundingRect());

            if(m_ButtonState == Hovered || m_ButtonState == Pressed){
                static const QBrush h = QBrush(QColor(180, 180, 180, 255));
                static const QBrush p = QBrush(QColor(150, 150, 150, 255));
                painter->setBrush(m_ButtonState == Hovered ? h : p);
                painter->setPen(Qt::NoPen);
                painter->setRenderHint(QPainter::Antialiasing, true);
                QPoint offset;
                switch(m_TreeBar->orientation()){
                case Qt::Horizontal: offset = QPoint(-7, -8); break;
                case Qt::Vertical:   offset = QPoint(-8, -7); break;
                }
                painter->drawRoundedRect(QRect(boundingRect().center().toPoint() + offset,
                                               QSize(13, 13)), 1, 1);
            }
            static QPixmap plus = QPixmap(":/resources/treebar/plus.png");
            QPoint offset;
            switch(m_TreeBar->orientation()){
            case Qt::Horizontal: offset = QPoint(-6, -7); break;
            case Qt::Vertical:   offset = QPoint(-7, -6); break;
            }
            painter->drawPixmap
                (QRect(boundingRect().center().toPoint() + offset,
                       plus.size()),
                 plus, QRect(QPoint(), plus.size()));
        }
    protected:
        void mousePressEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mousePressEvent(ev);
        }
        void mouseReleaseEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mouseReleaseEvent(ev);

            if(ev->button() == Qt::LeftButton){
                if((ev->buttonDownScreenPos(Qt::LeftButton) - ev->screenPos()).manhattanLength() < 4){
                    QMenu *menu = static_cast<LayerItem*>(parentItem())->MakeNodeMenu();
                    menu->exec(ev->screenPos());
                    delete menu;
                }
            } else if(ev->button() == Qt::RightButton){
                if((ev->buttonDownScreenPos(Qt::RightButton) - ev->screenPos()).manhattanLength() < 4){
                    QMenu *menu = static_cast<LayerItem*>(parentItem())->LayerMenu();
                    menu->exec(ev->screenPos());
                    delete menu;
                }
            }
        }
    };

    class ScrollButton : public GraphicsButton {
    public:
        ScrollButton(TreeBank *tb, TreeBar *bar, QGraphicsItem *parent = 0)
            : GraphicsButton(tb, bar, parent)
        {
            {
                static const QColor beg = QColor(240, 240, 240, 255);
                static const QColor end = QColor(240, 240, 240, 0);
                m_Gradient.setColorAt(static_cast<qreal>(0), beg);
                m_Gradient.setColorAt(static_cast<qreal>(0.5), beg);
                m_Gradient.setColorAt(static_cast<qreal>(1), end);
            }
            {
                static const QColor beg = QColor(210, 210, 210, 255);
                static const QColor end = QColor(210, 210, 210, 0);
                m_HoveredGradient.setColorAt(static_cast<qreal>(0), beg);
                m_HoveredGradient.setColorAt(static_cast<qreal>(0.5), beg);
                m_HoveredGradient.setColorAt(static_cast<qreal>(1), end);
            }
            {
                static const QColor beg = QColor(128, 128, 128, 255);
                static const QColor end = QColor(128, 128, 128, 0);
                m_PressedGradient.setColorAt(static_cast<qreal>(0), beg);
                m_PressedGradient.setColorAt(static_cast<qreal>(0.5), beg);
                m_PressedGradient.setColorAt(static_cast<qreal>(1), end);
            }
        }
        ~ScrollButton(){}

        LayerItem *Layer() const {
            return static_cast<LayerItem*>(parentItem());
        }

    protected:
        QLinearGradient m_Gradient;
        QLinearGradient m_HoveredGradient;
        QLinearGradient m_PressedGradient;

        void SetBGBrush(QPainter *painter, QPointF start, QPointF stop){
            m_Gradient.setStart(start);
            m_Gradient.setFinalStop(stop);
            QBrush brush = QBrush(m_Gradient);
            painter->setBrush(brush);
        }
        void SetFGBrush(QPainter *painter, QPointF start, QPointF stop){
            QBrush brush;
            switch(m_ButtonState){
            case NotHovered:
                painter->setBrush(Qt::NoBrush);
                return;
            case Hovered:
                m_HoveredGradient.setStart(start);
                m_HoveredGradient.setFinalStop(stop);
                brush = QBrush(m_HoveredGradient);
                break;
            case Pressed:
                m_PressedGradient.setStart(start);
                m_PressedGradient.setFinalStop(stop);
                brush = QBrush(m_PressedGradient);
                break;
            }
            painter->setBrush(brush);
        }
    };

    class LeftScrollButton : public ScrollButton {
    public:
        LeftScrollButton(TreeBank *tb, TreeBar *bar, QGraphicsItem *parent = 0)
            : ScrollButton(tb, bar, parent)
        {
        }
        QRectF boundingRect() const Q_DECL_OVERRIDE {
            QRectF rect = parentItem()->boundingRect();
            rect.setLeft(17);
            rect.setWidth(15);
            return rect;
        }

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) Q_DECL_OVERRIDE {
            Q_UNUSED(option); Q_UNUSED(widget);
            QRectF rect = boundingRect();
            painter->setPen(Qt::NoPen);
            if(!Application::EnableTransparentBar()){
                SetBGBrush(painter, QPointF(rect.left(), 0), QPointF(rect.right(), 0));
                painter->drawRect(rect);
            }
            SetFGBrush(painter, QPointF(rect.left(), 0), QPointF(rect.right(), 0));
            rect.setTop(rect.top() + m_TreeBar->GetHorizontalNodeHeight() / 2 - 6);
            rect.setHeight(13);
            painter->drawRect(rect);

            static QPixmap left = QPixmap(":/resources/treebar/left.png");
            painter->drawPixmap
                (QRect(boundingRect().topLeft().toPoint() +
                       QPoint(-1, m_TreeBar->GetHorizontalNodeHeight() / 2 - 5),
                       left.size()),
                 left, QRect(QPoint(), left.size()));
        }
    protected:
        void mousePressEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mousePressEvent(ev);
            Layer()->StartScrollUpTimer();
        }
        void mouseReleaseEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mouseReleaseEvent(ev);
            Layer()->AutoScrollStopOrScroll(-m_TreeBar->GetHorizontalNodeWidth());
        }
    };

    class RightScrollButton : public ScrollButton {
    public:
        RightScrollButton(TreeBank *tb, TreeBar *bar, QGraphicsItem *parent = 0)
            : ScrollButton(tb, bar, parent)
        {
        }
        QRectF boundingRect() const Q_DECL_OVERRIDE {
            QRectF rect = parentItem()->boundingRect();
            rect.setLeft(rect.right() - 15 - 17);
            rect.setWidth(15);
            return rect;
        }

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) Q_DECL_OVERRIDE {
            Q_UNUSED(option); Q_UNUSED(widget);
            QRectF rect = boundingRect();
            painter->setPen(Qt::NoPen);
            if(!Application::EnableTransparentBar()){
                SetBGBrush(painter, QPointF(rect.right(), 0), QPoint(rect.left(), 0));
                painter->drawRect(rect);
            }
            SetFGBrush(painter, QPointF(rect.right(), 0), QPoint(rect.left(), 0));
            rect.setTop(rect.top() + m_TreeBar->GetHorizontalNodeHeight() / 2 - 6);
            rect.setHeight(13);
            painter->drawRect(rect);

            static QPixmap right = QPixmap(":/resources/treebar/right.png");
            painter->drawPixmap
                (QRect(boundingRect().topRight().toPoint() +
                       QPoint(-10, m_TreeBar->GetHorizontalNodeHeight() / 2 - 5),
                       right.size()),
                 right, QRect(QPoint(), right.size()));
        }
    protected:
        void mousePressEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mousePressEvent(ev);
            Layer()->StartScrollDownTimer();
        }
        void mouseReleaseEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mouseReleaseEvent(ev);
            Layer()->AutoScrollStopOrScroll(m_TreeBar->GetHorizontalNodeWidth());
        }
    };

    class UpScrollButton : public ScrollButton {
    public:
        UpScrollButton(TreeBank *tb, TreeBar *bar, QGraphicsItem *parent = 0)
            : ScrollButton(tb, bar, parent)
        {
        }
        QRectF boundingRect() const Q_DECL_OVERRIDE {
            QRectF rect = parentItem()->boundingRect();
            rect.setTop(17);
            rect.setHeight(15);
            return rect;
        }

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) Q_DECL_OVERRIDE {
            Q_UNUSED(option); Q_UNUSED(widget);
            QRectF rect = boundingRect();
            painter->setPen(Qt::NoPen);
            if(!Application::EnableTransparentBar()){
                SetBGBrush(painter, QPointF(0, rect.top()), QPointF(0, rect.bottom()));
                painter->drawRect(rect);
            }
            SetFGBrush(painter, QPointF(0, rect.top()), QPointF(0, rect.bottom()));
            rect.setLeft(rect.left() + m_TreeBar->GetVerticalNodeWidth() / 2 - 6);
            rect.setWidth(13);
            painter->drawRect(rect);

            static QPixmap up = QPixmap(":/resources/treebar/up.png");
            painter->drawPixmap
                (QRect(boundingRect().topLeft().toPoint() +
                       QPoint(m_TreeBar->GetVerticalNodeWidth() / 2 - 5, -1),
                       up.size()),
                 up, QRect(QPoint(), up.size()));
        }
    protected:
        void mousePressEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mousePressEvent(ev);
            Layer()->StartScrollUpTimer();
        }
        void mouseReleaseEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mouseReleaseEvent(ev);
            Layer()->AutoScrollStopOrScroll(-m_TreeBar->GetVerticalNodeHeight() * 3.0);
        }
    };

    class DownScrollButton : public ScrollButton {
    public:
        DownScrollButton(TreeBank *tb, TreeBar *bar, QGraphicsItem *parent = 0)
            : ScrollButton(tb, bar, parent)
        {
        }
        QRectF boundingRect() const Q_DECL_OVERRIDE {
            QRectF rect = parentItem()->boundingRect();
            rect.setTop(rect.bottom() - 15 - 17);
            rect.setHeight(15);
            return rect;
        }

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) Q_DECL_OVERRIDE {
            Q_UNUSED(option); Q_UNUSED(widget);
            QRectF rect = boundingRect();
            painter->setPen(Qt::NoPen);
            if(!Application::EnableTransparentBar()){
                SetBGBrush(painter, QPointF(0, rect.bottom()), QPointF(0, rect.top()));
                painter->drawRect(rect);
            }
            SetFGBrush(painter, QPointF(0, rect.bottom()), QPointF(0, rect.top()));
            rect.setLeft(rect.left() + m_TreeBar->GetVerticalNodeWidth() / 2 - 6);
            rect.setWidth(13);
            painter->drawRect(rect);

            static QPixmap down = QPixmap(":/resources/treebar/down.png");
            painter->drawPixmap
                (QRect(boundingRect().bottomLeft().toPoint() +
                       QPoint(m_TreeBar->GetVerticalNodeWidth() / 2 - 5, -10),
                       down.size()),
                 down, QRect(QPoint(), down.size()));
        }
    protected:
        void mousePressEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mousePressEvent(ev);
            Layer()->StartScrollDownTimer();
        }
        void mouseReleaseEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mouseReleaseEvent(ev);
            Layer()->AutoScrollStopOrScroll(m_TreeBar->GetVerticalNodeHeight() * 3.0);
        }
    };

    class LayerScroller : public GraphicsButton {
    public:
        LayerScroller(TreeBank *tb, TreeBar *bar, QGraphicsItem *parent = 0)
            : GraphicsButton(tb, bar, parent)
        {
            setZValue(FRINGE_BUTTON_LAYER);
        }

        QRectF boundingRect() const Q_DECL_OVERRIDE {
            const LayerItem *layer = static_cast<LayerItem*>(parentItem());
            const qreal cur = layer->GetScroll();
            const qreal min = layer->MinScroll();
            const qreal max = layer->MaxScroll();
            if(max <= min) return QRectF();

            QRectF rect = layer->boundingRect();
            qreal rate = (cur - min) / (max - min);

            switch(m_TreeBar->orientation()){
            case Qt::Horizontal:
                if(m_ButtonState == NotHovered)
                    rect.setTop(rect.bottom() - 13);
                else rect.setTop(rect.bottom() - 16);
                rect.setBottom(rect.bottom() - 1);
                rect.setLeft((rect.width()
                              - FRINGE_BUTTON_SIZE * 2.0 - SCROLL_INDICATOR_SIZE) * rate
                             + FRINGE_BUTTON_SIZE - 1);
                rect.setWidth(SCROLL_INDICATOR_SIZE);
                break;
            case Qt::Vertical:
                if(m_ButtonState == NotHovered)
                    rect.setLeft(rect.width() - 13);
                else rect.setLeft(rect.width() - 16);
                rect.setRight(rect.right() - 1);
                rect.setTop((rect.height()
                             - FRINGE_BUTTON_SIZE * 2.0 - SCROLL_INDICATOR_SIZE) * rate
                            + FRINGE_BUTTON_SIZE - 1);
                rect.setHeight(SCROLL_INDICATOR_SIZE);
                break;
            }
            return rect;
        }

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) Q_DECL_OVERRIDE {
            Q_UNUSED(option); Q_UNUSED(widget);
            QRectF rect = boundingRect();
            if(!rect.isValid()) return;
            painter->setPen(Qt::NoPen);
            static const QBrush nb = QBrush(QColor(0, 0, 0, 50));
            static const QBrush hb = QBrush(QColor(0, 0, 0, 80));
            static const QBrush pb = QBrush(QColor(0, 0, 0, 110));
            switch(m_ButtonState){
            case NotHovered: painter->setBrush(nb); break;
            case Hovered:    painter->setBrush(hb); break;
            case Pressed:    painter->setBrush(pb); break;
            }
            rect.setTop(rect.top() + 1);
            rect.setLeft(rect.left() + 1);
            painter->drawRect(rect);
        }
    protected:
        void mouseMoveEvent(QGraphicsSceneMouseEvent *ev) Q_DECL_OVERRIDE {
            GraphicsButton::mouseMoveEvent(ev);
            LayerItem *layer = static_cast<LayerItem*>(parentItem());
            const qreal min = layer->MinScroll();
            const qreal max = layer->MaxScroll();
            if(max <= min) return;
            qreal rate;
            switch(m_TreeBar->orientation()){
            case Qt::Horizontal:
                rate = (ev->scenePos().x() - FRINGE_BUTTON_SIZE - 10) /
                    (layer->boundingRect().width() - FRINGE_BUTTON_SIZE * 2.0 - SCROLL_INDICATOR_SIZE);
                break;
            case Qt::Vertical:
                rate = (ev->scenePos().y() - FRINGE_BUTTON_SIZE - 10) /
                    (layer->boundingRect().height() - FRINGE_BUTTON_SIZE * 2.0 - SCROLL_INDICATOR_SIZE);
                break;
            }
            layer->SetScroll(min + (max - min) * rate);
            layer->ResetTargetScroll();
        }
        void hoverLeaveEvent(QGraphicsSceneHoverEvent *ev) Q_DECL_OVERRIDE {
            QRectF rect = boundingRect();
            GraphicsButton::hoverLeaveEvent(ev);
            scene()->update(rect);
        }
    };

    class InsertPosition : public QGraphicsItem {
    public:
        InsertPosition(TreeBank *tb, TreeBar *bar, QGraphicsItem *parent = 0)
            : QGraphicsItem(parent)
        {
            setZValue(DRAGGING_NODE_LAYER);
            m_TreeBank = tb;
            m_TreeBar = bar;
            m_Target = 0;
            m_Where = NoWhere;
        }

        enum Where{
            NoWhere,
            LeftOfTarget,
            RightOfTarget,
        } m_Where;

        void SetTarget(NodeItem *item, Where where = NoWhere){
            if(m_TreeBar->orientation() == Qt::Horizontal && item != m_Target){
                if(m_Target) m_Target->SetHoveredWithItem(false);
                if(item) item->SetHoveredWithItem(true);
            }
            QRectF rect = boundingRect();
            m_Target = item;
            m_Where = where;
            if(rect != boundingRect()){
                scene()->update(rect);
                scene()->update(boundingRect());
            }
        }
        NodeItem *GetTarget(){
            return m_Target;
        }
        Where GetWhere(){
            return m_Where;
        }

        QRectF boundingRect() const Q_DECL_OVERRIDE {
            if(!m_Target || m_Where == NoWhere) return QRectF();
            QRectF rect = m_Target->boundingRect();
            switch(m_TreeBar->orientation()){
            case Qt::Horizontal:
                if(m_Where == LeftOfTarget)
                    rect.moveRight(rect.center().x());
                else rect.moveLeft(rect.center().x());
                break;
            case Qt::Vertical:
                if(m_Where == LeftOfTarget)
                    rect.moveBottom(rect.center().y());
                else rect.moveTop(rect.center().y());
                break;
            }
            return rect;
        }
        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) Q_DECL_OVERRIDE {
            Q_UNUSED(option); Q_UNUSED(widget);
            if(!m_Target) return;
            static const QBrush brush(QColor(0, 0, 0, 48));
            painter->setBrush(brush);
            painter->setPen(Qt::NoPen);
            painter->drawRect(boundingRect());
        }
    private:
        TreeBank *m_TreeBank;
        TreeBar *m_TreeBar;
        NodeItem *m_Target;
    };
}

TreeBar::TreeBar(TreeBank *tb, QWidget *parent)
    : QToolBar(tr("TreeBar"), parent)
{
    m_TreeBank = tb;
    m_Scene = new QGraphicsScene(this);
    m_Scene->setSceneRect(QRect(0, 0, width(), height()));
    m_View = new GraphicsView(m_Scene, this);
    m_View->setFrameShape(QGraphicsView::NoFrame);
    m_View->setAcceptDrops(true);
    m_ResizeGrip = new ResizeGrip(this);
    m_OverrideSize = QSize();
    m_LayerList = QList<LayerItem*>();
    m_AutoUpdateTimerId = 0;
    m_HorizontalNodeWidth = 0;
    m_HorizontalNodeHeight = 0;
    m_VerticalNodeHeight = 0;
    m_VerticalNodeWidth = 0;
    m_LastAction = None;
    m_TabWindow = 0;
    m_HotSpot = QPoint();

    addWidget(m_View);
    setObjectName(QStringLiteral("TreeBar"));
    setContextMenuPolicy(Qt::PreventContextMenu);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setAcceptDrops(true);
    setMouseTracking(true);

    if(Application::EnableTransparentBar()){
        m_View->setBackgroundBrush(Qt::transparent);
        m_View->setStyleSheet("QGraphicsView{ background: transparent}");
        setAttribute(Qt::WA_TranslucentBackground);
    } else {
        m_View->setBackgroundBrush(QColor(240, 240, 240, 255));
    }
}

TreeBar::~TreeBar(){}

void TreeBar::Initialize(){
    LoadSettings();
}

void TreeBar::LoadSettings(){
    Settings &s = Application::GlobalSettings();

    m_EnableAnimation     = s.value(QStringLiteral("treebar/@EnableAnimation"),    true ).value<bool>();
    m_EnableCloseButton   = s.value(QStringLiteral("treebar/@EnableCloseButton"),  true ).value<bool>();
    m_EnableCloneButton   = s.value(QStringLiteral("treebar/@EnableCloneButton"),  true ).value<bool>();
    m_ScrollToSwitchNode  = s.value(QStringLiteral("treebar/@ScrollToSwitchNode"), false).value<bool>();
    m_WheelClickToClose   = s.value(QStringLiteral("treebar/@WheelClickToClose"),  true ).value<bool>();
}

void TreeBar::SaveSettings(){
    Settings &s = Application::GlobalSettings();

    s.setValue(QStringLiteral("treebar/@EnableAnimation"),     m_EnableAnimation);
    s.setValue(QStringLiteral("treebar/@EnableCloseButton"),   m_EnableCloseButton);
    s.setValue(QStringLiteral("treebar/@EnableCloneButton"),   m_EnableCloneButton);
    s.setValue(QStringLiteral("treebar/@ScrollToSwitchNode"),  m_ScrollToSwitchNode);
    s.setValue(QStringLiteral("treebar/@WheelClickToClose"),   m_WheelClickToClose);
}

bool TreeBar::EnableAnimation(){
    return m_EnableAnimation;
}

bool TreeBar::EnableCloseButton(){
    return m_EnableCloseButton;
}

bool TreeBar::EnableCloneButton(){
    return m_EnableCloneButton;
}

bool TreeBar::ScrollToSwitchNode(){
    return m_ScrollToSwitchNode;
}

bool TreeBar::WheelClickToClose(){
    return m_WheelClickToClose;
}

void TreeBar::ToggleEnableAnimation(){
    m_EnableAnimation = !m_EnableAnimation;
}

void TreeBar::ToggleEnableCloseButton(){
    m_EnableCloseButton = !m_EnableCloseButton;
}

void TreeBar::ToggleEnableCloneButton(){
    m_EnableCloneButton = !m_EnableCloneButton;
}

void TreeBar::ToggleScrollToSwitchNode(){
    m_ScrollToSwitchNode = !m_ScrollToSwitchNode;
}

void TreeBar::ToggleWheelClickToClose(){
    m_WheelClickToClose = !m_WheelClickToClose;
}

void TreeBar::SetHorizontalNodeWidth(int width){
    m_HorizontalNodeWidth = width;
    CollectNodes();
}

void TreeBar::SetVerticalNodeHeight(int height){
    m_VerticalNodeHeight = height;
    CollectNodes();
}

int TreeBar::GetHorizontalNodeWidth() const {
    return m_HorizontalNodeWidth;
}

int TreeBar::GetHorizontalNodeHeight() const {
    if(orientation() == Qt::Horizontal && m_LayerList.length())
        return (height() - 4) / m_LayerList.length() - 3;
    return 0;
}

int TreeBar::GetVerticalNodeHeight() const {
    return m_VerticalNodeHeight;
}

int TreeBar::GetVerticalNodeWidth() const {
    if(orientation() == Qt::Vertical && m_LayerList.length())
        return width() - 7;
    return 0;
}

int TreeBar::MaxWidth() const {
    return parentWidget()->width();
}

int TreeBar::MinWidth() const {
    return TREEBAR_VERTICAL_NODE_MINIMUM_WIDTH + 7;
}

int TreeBar::MaxHeight() const {
    return qMin((TREEBAR_HORIZONTAL_NODE_MAXIMUM_HEIGHT + 3) * m_LayerList.length() + 4,
                parentWidget()->height());
}

int TreeBar::MinHeight() const {
    return (TREEBAR_HORIZONTAL_NODE_MINIMUM_HEIGHT + 3) * m_LayerList.length() + 4;
}

void TreeBar::SetStat(QStringList list){
    if(list.length() == 4){
        m_HorizontalNodeWidth = list[0].toInt();
        m_HorizontalNodeHeight = list[1].toInt();
        m_VerticalNodeHeight = list[2].toInt();
        m_VerticalNodeWidth = list[3].toInt();
    } else if(list.length() == 2){
        m_HorizontalNodeWidth = TREEBAR_HORIZONTAL_NODE_DEFAULT_WIDTH;
        m_HorizontalNodeHeight = list[0].toInt();
        m_VerticalNodeHeight = TREEBAR_VERTICAL_NODE_DEFAULT_HEIGHT;
        m_VerticalNodeWidth = list[1].toInt();
    }
    if(!m_HorizontalNodeWidth)
        m_HorizontalNodeWidth = TREEBAR_HORIZONTAL_NODE_DEFAULT_WIDTH;
    if(!m_HorizontalNodeHeight)
        m_HorizontalNodeHeight = TREEBAR_HORIZONTAL_NODE_DEFAULT_HEIGHT;
    if(!m_VerticalNodeHeight)
        m_VerticalNodeHeight = TREEBAR_VERTICAL_NODE_DEFAULT_HEIGHT;
    if(!m_VerticalNodeWidth)
        m_VerticalNodeWidth = TREEBAR_VERTICAL_NODE_DEFAULT_WIDTH;
}

QStringList TreeBar::GetStat() const {
    return QStringList()
        << QStringLiteral("%1").arg(m_HorizontalNodeWidth)
        << QStringLiteral("%1").arg(m_HorizontalNodeHeight)
        << QStringLiteral("%1").arg(m_VerticalNodeHeight)
        << QStringLiteral("%1").arg(m_VerticalNodeWidth);
}

void TreeBar::ShowTabWindow(const QPoint &cursorPos, Node *node){
    QPoint pos = parentWidget()->pos();
    m_HotSpot = cursorPos - pos;
    if(m_TabWindow){
        m_TabWindow->move(pos);
        m_TabWindow->show();
    } else {
        m_TabWindow = Application::NewWindow(0, pos);

        View *view = node->GetView();
        if(view && view->visible()){
            TreeBank *tb = view->GetTreeBank();
            NodeList list = node->GetSiblings();
            qSort(list.begin(), list.end(), [](Node *n1, Node *n2){
                return n1->GetLastAccessDate() > n2->GetLastAccessDate();
            });
            tb->blockSignals(true);
            foreach(Node *nd, list){
                if(nd != node && !nd->IsDirectory())
                    if(tb->SetCurrent(nd)) break;
            }
            tb->blockSignals(false);
        }
        m_Scene->update();
        m_TabWindow->GetTreeBank()->SetCurrent(node);
    }
    foreach(LayerItem *layer, GetLayerList()){
        layer->AutoScrollStop();
    }
}

void TreeBar::MoveTabWindow(const QPoint &cursorPos){
    if(m_TabWindow) m_TabWindow->move(cursorPos - m_HotSpot);
}

void TreeBar::CloseTabWindow(){
    if(m_TabWindow){
        if(m_TabWindow->isVisible()){
            m_TabWindow = 0;
        } else {
            MainWindow *win = static_cast<MainWindow*>(parentWidget());
            Application::CloseWindow(m_TabWindow);
            Application::SetCurrentWindow(win);
            m_TabWindow = 0;
            win->raise();
            win->activateWindow();
            setFocus();
        }
    }
}

void TreeBar::HideTabWindow(){
    if(m_TabWindow) m_TabWindow->hide();
}

bool TreeBar::TabWindowVisible(){
    return m_TabWindow && m_TabWindow->isVisible();
}

void TreeBar::Adjust(){
    m_OverrideSize = size();
    switch(orientation()){
    case Qt::Horizontal:
        if(GetHorizontalNodeHeight() != TREEBAR_VERTICAL_DEFAULT_HEIGHT - 7)
            m_HorizontalNodeHeight = GetHorizontalNodeHeight();
        break;
    case Qt::Vertical:
        if(GetVerticalNodeWidth() != TREEBAR_HORIZONTAL_DEFAULT_WIDTH - 7)
            m_VerticalNodeWidth = GetVerticalNodeWidth();
        break;
    }
    updateGeometry();
}

void TreeBar::ClearLowerLayer(int index){
    int i = 0;
    foreach(LayerItem *layer, m_LayerList){
        if(i > index){
            layer->hide();
            layer->deleteLater();
            m_LayerList.removeOne(layer);
        }
        i++;
    }
}

QSize TreeBar::sizeHint() const {
    if(isFloating()){
        switch(orientation()){
        case Qt::Horizontal:{
            int height = m_HorizontalNodeHeight;
            if(!height) height = TREEBAR_HORIZONTAL_NODE_DEFAULT_HEIGHT;
            return QSize(TREEBAR_HORIZONTAL_DEFAULT_WIDTH,
                         (height + 3) * m_LayerList.length() + 4);
        }
        case Qt::Vertical:{
            int width = m_VerticalNodeWidth;
            if(!width) width = TREEBAR_VERTICAL_NODE_DEFAULT_WIDTH;
            return QSize(width + 7,
                         TREEBAR_VERTICAL_DEFAULT_HEIGHT);
        }
        }
    }

    if(m_OverrideSize.isValid()){
        int width  = m_OverrideSize.width();
        int height = m_OverrideSize.height();
        Cramp(width,  MinWidth(),  MaxWidth());
        Cramp(height, MinHeight(), MaxHeight());
        return QSize(width, height);
    }

    return QSize(TREEBAR_VERTICAL_NODE_MINIMUM_WIDTH + 7,
                 (TREEBAR_HORIZONTAL_NODE_MINIMUM_HEIGHT + 3) * m_LayerList.length() + 4);
}

QSize TreeBar::minimumSizeHint() const {
    return QSize(0, 0);
}

QMenu *TreeBar::TreeBarMenu(){
    QMenu *menu = new QMenu(this);
    AddTreeBarMenu(menu);
    return menu;
}

void TreeBar::AddTreeBarMenu(QMenu *menu){
    menu->addAction(m_TreeBank->Action(TreeBank::_ToggleMenuBar));
    menu->addAction(m_TreeBank->Action(TreeBank::_ToggleTreeBar));
    menu->addAction(m_TreeBank->Action(TreeBank::_ToggleToolBar));

    menu->addSeparator();

    QMenu *settings = new QMenu(tr("TreeBarSettings"), menu);

    QWidgetAction *resizer = new QWidgetAction(settings);
    QWidget *widget = new QWidget(settings);
    QHBoxLayout *layout = new QHBoxLayout();
    QSlider *slider = new QSlider(Qt::Horizontal);
    QLabel *label = 0;

    switch(orientation()){
    case Qt::Horizontal:{
        label = new QLabel(tr("Width"));
        slider->setMinimum(TREEBAR_HORIZONTAL_NODE_MINIMUM_WIDTH);
        slider->setMaximum(TREEBAR_HORIZONTAL_NODE_MAXIMUM_WIDTH);
        slider->setValue(GetHorizontalNodeWidth());
        connect(slider, &QSlider::valueChanged, [this](int i){
            m_HorizontalNodeWidth = i;
            CollectNodes();
        });
        break;
    }
    case Qt::Vertical:{
        label = new QLabel(tr("Height"));
        slider->setMinimum(TREEBAR_VERTICAL_NODE_MINIMUM_HEIGHT);
        slider->setMaximum(TREEBAR_VERTICAL_NODE_MAXIMUM_HEIGHT);
        slider->setValue(GetVerticalNodeHeight());
        connect(slider, &QSlider::valueChanged, [this](int i){
            m_VerticalNodeHeight = i;
            CollectNodes();
        });
        break;
    }
    }

    layout->addWidget(slider);
    layout->addWidget(label);
    widget->setLayout(layout);
    resizer->setDefaultWidget(widget);
    settings->addAction(resizer);

    QAction *animation = new QAction(settings);
    animation->setText(tr("EnableAnimation"));
    animation->setCheckable(true);
    animation->setChecked(m_EnableAnimation);
    animation->connect(animation, &QAction::triggered,
                       [this](){ m_EnableAnimation = !m_EnableAnimation;});
    settings->addAction(animation);

    QAction *closeButton = new QAction(settings);
    closeButton->setText(tr("EnableCloseButton"));
    closeButton->setCheckable(true);
    closeButton->setChecked(m_EnableCloseButton);
    closeButton->connect(closeButton, &QAction::triggered,
                         [this](){ m_EnableCloseButton = !m_EnableCloseButton;});
    settings->addAction(closeButton);

    QAction *cloneButton = new QAction(menu);
    cloneButton->setText(tr("EnableCloneButton"));
    cloneButton->setCheckable(true);
    cloneButton->setChecked(m_EnableCloneButton);
    cloneButton->connect(cloneButton, &QAction::triggered,
                         [this](){ m_EnableCloneButton = !m_EnableCloneButton;});
    settings->addAction(cloneButton);

    menu->addMenu(settings);
}

void TreeBar::CollectNodes(){
    int previousHeight = GetHorizontalNodeHeight();
    int previousWidth  = GetVerticalNodeWidth();
    QList<qreal> previousScroll;
    foreach(LayerItem *layer, m_LayerList){
        previousScroll << layer->GetScroll();
    }
    m_LayerList.clear();
    m_Scene->clear();
    m_Scene->addItem(new TableButton(m_TreeBank, this));

    Node *cur = m_TreeBank->GetCurrentViewNode();

    switch(orientation()){
    case Qt::Horizontal:{
        if(!previousHeight) previousHeight = m_HorizontalNodeHeight;
        if(!previousHeight) previousHeight = TREEBAR_HORIZONTAL_NODE_DEFAULT_HEIGHT;

        if(!cur || TreeBank::IsTrash(cur)){
            LayerItem *layer = new LayerItem(m_TreeBank, this, 0);
            m_LayerList << layer;
            m_Scene->addItem(layer);
            NodeList children = TreeBank::GetViewRoot()->GetChildren();
            for(int i = 0; i < children.length(); i++){
                layer->CreateNodeItem(children[i], 0, i, 0, previousHeight);
            }
        } else {
            Node *nd = cur;
            NodeList path;
            while(nd && nd->GetParent()){
                path.prepend(nd);
                nd = nd->GetParent();
            }
            for(int i = 0; i < path.length(); i++){
                LayerItem *layer = new LayerItem(m_TreeBank, this, path[i]);
                m_LayerList << layer;
                m_Scene->addItem(layer);
                NodeList siblings = layer->GetNode()->GetSiblings();
                for(int j = 0; j < siblings.length(); j++){
                    layer->CreateNodeItem(siblings[j], i, j, 0, previousHeight);
                }
            }
        }

        resize(width(), (previousHeight + 3) * m_LayerList.length() + 4);
        QSize size = QSize(width() - 12, height() - 4);
        if(m_View->size() != size) m_View->resize(size);
        m_Scene->setSceneRect(0.0, 0.0, size.width(), size.height());
        break;
    }
    case Qt::Vertical:{
        if(!previousWidth) previousWidth = m_VerticalNodeWidth;
        if(!previousWidth) previousWidth = TREEBAR_VERTICAL_NODE_DEFAULT_WIDTH;

        Node *nd = cur;
        NodeList path;
        if(!cur || TreeBank::IsTrash(cur)){
            cur = nd = 0;
        } else {
            cur->ResetPrimaryPath();
            while(nd && nd->GetParent()){
                path.prepend(nd);
                nd = nd->GetParent();
            }
        }
        LayerItem *layer = new LayerItem(m_TreeBank, this, cur);
        m_LayerList << layer;
        m_Scene->addItem(layer);

        int i = 0;
        std::function<void(Node*, int)> collectNode;

        collectNode = [&](Node *nd, int nest){
            layer->CreateNodeItem(nd, i, 0, nest, previousWidth);
            i++;
            if(nd->IsDirectory()){
                if(nd->GetFolded()){
                    if(path.contains(nd) && nd->GetPrimary())
                        collectNode(nd->GetPrimary(), nest + 1);
                } else {
                    foreach(Node *child, nd->GetChildren()){
                        collectNode(child, nest + 1);
                    }
                }
            }
        };
        foreach(Node *nd, TreeBank::GetViewRoot()->GetChildren()){
            collectNode(nd, 0);
        }
        resize(previousWidth + 7, height());
        QSize size = QSize(width() - 4, height() - 12);
        if(m_View->size() != size) m_View->resize(size);
        m_Scene->setSceneRect(0.0, 0.0, size.width(), size.height());
        break;
    }
    }

    Adjust();

    if(m_LastAction != None && previousScroll.length() == m_LayerList.length()){
        for(int i = 0; i < m_LayerList.length(); i++){
            m_LayerList[i]->Adjust(previousScroll[i]);
        }
    } else {
        foreach(LayerItem *layer, m_LayerList){ layer->Adjust();}
    }
}

void TreeBar::OnTreeStructureChanged(){
    CollectNodes();
    m_LastAction = TreeStructureChanged;
}

void TreeBar::OnNodeCreated(NodeList &nds){
    switch(orientation()){
    case Qt::Horizontal:{
        int previousHeight = GetHorizontalNodeHeight();
        if(!previousHeight) previousHeight = m_HorizontalNodeHeight;
        if(!previousHeight) previousHeight = TREEBAR_HORIZONTAL_NODE_DEFAULT_HEIGHT;

        for(int i = 0; i < m_LayerList.length(); i++){
            LayerItem *layer = m_LayerList[i];
            QList<NodeItem*> items = layer->GetNodeItems();
            if(items.length()){
                foreach(NodeItem *item, items){
                    item->setSelected(false);
                }
                foreach(Node *nd, nds){
                    if(nd->IsSiblingOf(items.first()->GetNode())){
                        int j = nd->Index();
                        NodeItem *item = layer->CreateNodeItem(nd, i, j, 0, previousHeight);
                        layer->SetFocusedNode(item);
                        layer->CorrectOrder();
                        item->OnCreated(item->GetRect());
                    }
                }
            } else {
                int j = 0;
                foreach(Node *nd, nds){
                    if(i && m_LayerList[i-1]->GetNode() == nd->GetParent()){
                        NodeItem *item = layer->CreateNodeItem(nd, i, j, 0, previousHeight);
                        layer->SetFocusedNode(item);
                        layer->CorrectOrder();
                        item->OnCreated(item->GetRect());
                        j++;
                    } else {
                        CollectNodes(); // abort.
                        m_LastAction = NodeCreated;
                        return;
                    }
                }
            }
            layer->SetFocusedNode(0);
        }
        m_LastAction = NodeCreated;
        return;
    }
    case Qt::Vertical:{
        int previousWidth = GetVerticalNodeWidth();
        if(!previousWidth) previousWidth = m_VerticalNodeWidth;
        if(!previousWidth) previousWidth = TREEBAR_VERTICAL_NODE_DEFAULT_WIDTH;

        LayerItem *layer = m_LayerList.first();
        QList<NodeItem*> items = layer->GetNodeItems();
        if(!items.length()) break;
        foreach(NodeItem *item, items){
            item->setSelected(false);
        }

        int i = 0;
        std::function<void(Node*, int)> collectNode;

        collectNode = [&](Node *nd, int nest){
            NodeItem *item = layer->CreateNodeItem(nd, i, 0, nest, previousWidth);
            layer->SetFocusedNode(item);
            layer->CorrectOrder();
            item->SetNest(nest);
            QRectF rect = item->GetRect();
            rect.setLeft(nest * 20 + 1);
            item->OnCreated(rect);
            i++;
            if(nd->IsDirectory()){
                if(!nd->GetFolded()){
                    foreach(Node *child, nd->GetChildren()){
                        collectNode(child, nest + 1);
                    }
                }
            }
        };

        Node *upper = 0;
        Node *lower = 0;
        foreach(Node *nd, nds){
            NodeList siblings = nd->GetSiblings();
            if(siblings.length() == 1){
                CollectNodes(); // abort.
                m_LastAction = NodeCreated;
                return;
            }
            i = nd->Index();
            if(i >= 1 && i < siblings.length())
                upper = siblings[i-1];
            if(i >= 0 && i < siblings.length() - 1)
                lower = siblings[i+1];
            i = 0; // reuse.
            if(lower){
                foreach(NodeItem *item, items){
                    if(item->GetNode() == lower){
                        collectNode(nd, item->GetNest());
                        break;
                    }
                    i++;
                }
            } else if(upper){
                if(upper->IsDirectory()){
                    int nest = 0;

                    foreach(NodeItem *item, items){
                        if(nest && !item->GetNode()->IsDescendantOf(upper)) break;
                        i++;
                        if(item->GetNode() == upper) nest = item->GetNest();
                    }

                    collectNode(nd, nest);

                } else {
                    foreach(NodeItem *item, items){
                        i++;
                        if(item->GetNode() == upper){
                            collectNode(nd, item->GetNest());
                            break;
                        }
                    }
                }
            } else {
                CollectNodes(); // abort.
                m_LastAction = NodeCreated;
                return;
            }
        }
        m_LastAction = NodeCreated;
        return;
    }
    }
    CollectNodes();
    m_LastAction = NodeCreated;
}

void TreeBar::OnNodeDeleted(NodeList &nds){
    foreach(LayerItem *layer, m_LayerList){
        QList<NodeItem*> rem;
        foreach(NodeItem *item, layer->GetNodeItems()){
            Node *nd = item->GetNode();
            switch(orientation()){
            case Qt::Horizontal:
                if(nds.contains(nd)){
                    rem << item;
                }
                break;
            case Qt::Vertical:
                if(nds.contains(nd) ||
                   !nds.toSet().intersect(nd->GetAncestors().toSet()).isEmpty()){
                    rem << item;
                }
                break;
            }
        }
        layer->ScrollForDelete(rem.length());
        foreach(NodeItem *item, rem){
            item->setSelected(false);
            item->SetTargetPosition(QPointF(DBL_MAX, DBL_MAX));
            layer->SetFocusedNode(item);
            layer->CorrectOrder();
            QRectF rect = item->GetRect();
            switch(orientation()){
            case Qt::Horizontal: rect.setWidth(0);  break;
            case Qt::Vertical:   rect.setHeight(0); break;
            }
            item->OnDeleted(rect);
        }
        layer->SetFocusedNode(0);
    }
    m_LastAction = NodeDeleted;
}

void TreeBar::OnFoldedChanged(NodeList &nds){
    if(orientation() == Qt::Horizontal) return;
    int previousWidth = GetVerticalNodeWidth();
    if(!previousWidth) previousWidth = m_VerticalNodeWidth;
    if(!previousWidth) previousWidth = TREEBAR_VERTICAL_NODE_DEFAULT_WIDTH;
    LayerItem *layer = m_LayerList.first();
    if(layer->IsLocked()) return;
    QList<NodeItem*> &items = layer->GetNodeItems();
    foreach(NodeItem *item, items){
        item->setSelected(false);
    }
    foreach(Node *nd, nds){
        NodeList children = nd->GetChildren();
        if(nd->GetFolded()){

            // Fold directory.

            QList<NodeItem*> rem;
            foreach(NodeItem *item, items){
                if(item->GetNode()->GetAncestors().contains(nd))
                    rem << item;
            }
            if(rem.isEmpty()) continue;
            if(rem.length() < 256){
                OnNodeDeleted(children);
                continue;
            }
            layer->ScrollForDelete(rem.length());
            int beg = items.indexOf(rem.first());
            int end = items.indexOf(rem.last());
            QRectF target = items[beg]->GetRect();
            target.setHeight(0);
            int i = beg;
            for(; i <= end; i++){
                target.setLeft(items[i]->GetRect().left());
                items[i]->OnDeleted(target);
            }
            for(; i < items.length(); i++){
                items[i]->Slide(-rem.length());
            }
        } else {

            // Unfold directory.

            if(children.length() < 256){
                int i = 0;
                int nest = 0;
                NodeList rem;
                for(; i < items.length(); i++){
                    if(items[i]->GetNode() != nd) continue;
                    nest = items[i]->GetNest() + 1;
                    if(i != items.length() - 1 && items[i+1]->GetNest() == nest){
                        // to unfold but already displayed node(s).
                        for(int j = i+1; j < items.length() && items[j]->GetNest() == nest; j++){
                            rem << items[j]->GetNode();
                        }
                    }
                    i++; break;
                }
                if(!nest) continue;
                OnNodeDeleted(rem);
                std::reverse(children.begin(), children.end());
                foreach(Node *child, children){
                    NodeItem *item = layer->CreateNodeItem(child, i, 0, nest, previousWidth);
                    layer->SetFocusedNode(item);
                    layer->CorrectOrder();
                    item->SetNest(nest);
                    QRectF rect = item->GetRect();
                    rect.setLeft(nest * 20 + 1);
                    item->OnCreated(rect);
                }
                continue;
            }
            NodeItem *parent = 0;
            foreach(NodeItem *item, items){
                if(item->GetNode() == nd){ parent = item; break;}
            }
            if(!parent) continue;
            int nest = parent->GetNest() + 1;
            int base = items.indexOf(parent) + 1;
            int len = children.length();
            QRectF start = parent->GetRect();
            start.setLeft(start.left()+20);
            start.moveTop(start.top() + start.height());
            start.setHeight(0);
            for(int i = 0; i < len; i++){
                NodeItem *item = layer->CreateNodeItem(children[i], base + i, 0, nest, previousWidth);
                items.move(items.length() - 1, base + i);
                QRectF rect = item->GetRect();
                rect.setLeft(nest * 20 + 1);
                item->OnCreated(rect, start);
            }
            for(int i = base + len; i < items.length(); i++){
                items[i]->Slide(len);
            }
        }
    }

    if(TreeBar::EnableAnimation()){
        layer->LockWhileAnimating();
    }
    m_LastAction = FoldedChanged;
}

void TreeBar::OnCurrentChanged(Node *nd){
    switch(orientation()){
    case Qt::Horizontal:{
        if(!nd) break;
        int previousHeight = GetHorizontalNodeHeight();
        NodeList path;
        while(nd && nd->GetParent()){
            path.prepend(nd);
            nd = nd->GetParent();
        }
        int i = 0;
        bool branched = false;
        for(; i < path.length(); i++){
            LayerItem *layer = m_LayerList[i];
            Node *nd = path[i];
            branched = layer->GetNode() != nd;
            QList<NodeItem*> &items = layer->GetNodeItems();
            int index = -1;
            for(int j = 0; j < items.length(); j++){
                if(items[j]->GetNode() == nd){ index = j; break;}
            }
            for(int j = 0; j < items.length(); j++){
                NodeItem *item = items[j];
                item->SetFocused(item->GetNode() == nd);
                item->setSelected(false);
            }
            layer->SetNode(nd);
            layer->ResetTargetScroll();
            qreal target = m_HorizontalNodeWidth * (index + 0.5)
                - m_Scene->sceneRect().width() / 2.0 + FRINGE_BUTTON_SIZE;

            if(layer->GetAnimation()->state() == QAbstractAnimation::Running &&
               layer->GetAnimation()->endValue().isValid()){

                if(m_LastAction == NodeCreated)
                    target = qMax(target, layer->GetAnimation()->endValue().toReal());
                if(m_LastAction == NodeDeleted)
                    target = qMin(target, layer->GetAnimation()->endValue().toReal());
            }
            layer->Scroll(target - layer->GetScroll());
            if(branched) break;
        }

        ClearLowerLayer(i);

        for(i++; i < path.length(); i++){
            LayerItem *layer = new LayerItem(m_TreeBank, this, path[i]);
            m_LayerList << layer;
            m_Scene->addItem(layer);
            NodeList siblings = layer->GetNode()->GetSiblings();
            for(int j = 0; j < siblings.length(); j++){
                layer->CreateNodeItem(siblings[j], i, j, 0, previousHeight);
            }
            layer->Adjust();
        }
        resize(width(), (previousHeight + 3) * m_LayerList.length() + 4);
        Adjust();
        m_LastAction = CurrentChanged;
        return;
    }
    case Qt::Vertical:{
        if(!nd) break;

        LayerItem *layer = m_LayerList.first();
        QList<NodeItem*> &items = layer->GetNodeItems();
        int index = -1;
        for(int i = 0; i < items.length(); i++){
            if(items[i]->GetNode() == nd){ index = i; break;}
        }
        if(index == -1) break;
        for(int i = 0; i < items.length(); i++){
            NodeItem *item = items[i];
            item->SetFocused(item->GetNode() == nd);
            item->setSelected(false);
        }
        layer->SetNode(nd);
        layer->ResetTargetScroll();
        qreal target = m_VerticalNodeHeight * (index + 0.5)
            - m_Scene->sceneRect().height() / 2.0 + FRINGE_BUTTON_SIZE;

        if(layer->GetAnimation()->state() == QAbstractAnimation::Running &&
           layer->GetAnimation()->endValue().isValid()){

            if(m_LastAction == NodeCreated)
                target = qMax(target, layer->GetAnimation()->endValue().toReal());
            if(m_LastAction == NodeDeleted)
                target = qMin(target, layer->GetAnimation()->endValue().toReal());
        }
        layer->Scroll(target - layer->GetScroll());
        m_LastAction = CurrentChanged;
        return;
    }
    }
    CollectNodes();
    m_LastAction = CurrentChanged;
}

void TreeBar::StartAutoUpdateTimer(){
    if(m_AutoUpdateTimerId) killTimer(m_AutoUpdateTimerId);
    m_AutoUpdateTimerId = startTimer(200);
    connect(m_Scene, &QGraphicsScene::changed, this, &TreeBar::RestartAutoUpdateTimer);
}

void TreeBar::StopAutoUpdateTimer(){
    disconnect(m_Scene, &QGraphicsScene::changed, this, &TreeBar::RestartAutoUpdateTimer);
    if(m_AutoUpdateTimerId) killTimer(m_AutoUpdateTimerId);
    m_AutoUpdateTimerId = 0;
}

void TreeBar::RestartAutoUpdateTimer(){
    if(m_AutoUpdateTimerId) killTimer(m_AutoUpdateTimerId);
    if(m_View->scene()) m_AutoUpdateTimerId = startTimer(m_Scene->hasFocus() ? 200 : 1000);
    else m_AutoUpdateTimerId = 0;
}

void TreeBar::paintEvent(QPaintEvent *ev){
    QPainter painter(this);

    static const QBrush tb = QBrush(QColor(0, 0, 0, 1));
    static const QBrush nb = QBrush(QColor(240, 240, 240, 255));
    painter.setPen(Qt::NoPen);
    painter.setBrush(Application::EnableTransparentBar() ? tb : nb);
    foreach(QRect rect, ev->region().rects()){
        painter.drawRect(rect);
    }
    QStyle *s = style();
    QStyleOptionToolBar opt;
    initStyleOption(&opt);
    opt.rect = s->subElementRect(QStyle::SE_ToolBarHandle, &opt, this);
    if(opt.rect.isValid() && ev->region().contains(opt.rect))
        s->drawPrimitive(QStyle::PE_IndicatorToolBarHandle, &opt, &painter, this);
}

void TreeBar::resizeEvent(QResizeEvent *ev){
    int width = ev->size().width();
    int height = ev->size().height();

    switch(static_cast<MainWindow*>(parentWidget())->toolBarArea(this)){
    case Qt::TopToolBarArea:
        m_ResizeGrip->setGeometry(0, height-4, width, 4);
        break;
    case Qt::BottomToolBarArea:
        m_ResizeGrip->setGeometry(0, 0, width, 4);
        break;
    case Qt::LeftToolBarArea:
        m_ResizeGrip->setGeometry(width-4, 0, 4, height);
        break;
    case Qt::RightToolBarArea:
        m_ResizeGrip->setGeometry(0, 0, 4, height);
        break;
    default: break;
    }
    m_ResizeGrip->show();
    m_ResizeGrip->raise();
    QToolBar::resizeEvent(ev);
    m_Scene->setSceneRect(0.0, 0.0, m_View->width(), m_View->height());
    foreach(LayerItem *layer, m_LayerList){ layer->Adjust();}
}

void TreeBar::timerEvent(QTimerEvent *ev){
    QToolBar::timerEvent(ev);
    if(!isVisible()) return;
    if(parentWidget()->isActiveWindow() &&
       m_AutoUpdateTimerId && ev->timerId() == m_AutoUpdateTimerId){
        m_Scene->update();
    }
}

void TreeBar::showEvent(QShowEvent *ev){
    QToolBar::showEvent(ev);
    connect(m_TreeBank, &TreeBank::TreeStructureChanged, this, &TreeBar::OnTreeStructureChanged);
    connect(m_TreeBank, &TreeBank::NodeCreated,          this, &TreeBar::OnNodeCreated);
    connect(m_TreeBank, &TreeBank::NodeDeleted,          this, &TreeBar::OnNodeDeleted);
    connect(m_TreeBank, &TreeBank::FoldedChanged,        this, &TreeBar::OnFoldedChanged);
    connect(m_TreeBank, &TreeBank::CurrentChanged,       this, &TreeBar::OnCurrentChanged);
    connect(this,       &QToolBar::orientationChanged,   this, &TreeBar::CollectNodes);
    if(m_LayerList.isEmpty()) CollectNodes();
    StartAutoUpdateTimer();
}

void TreeBar::hideEvent(QHideEvent *ev){
    QToolBar::hideEvent(ev);
    disconnect(m_TreeBank, &TreeBank::TreeStructureChanged, this, &TreeBar::OnTreeStructureChanged);
    disconnect(m_TreeBank, &TreeBank::NodeCreated,          this, &TreeBar::OnNodeCreated);
    disconnect(m_TreeBank, &TreeBank::NodeDeleted,          this, &TreeBar::OnNodeDeleted);
    disconnect(m_TreeBank, &TreeBank::FoldedChanged,        this, &TreeBar::OnFoldedChanged);
    disconnect(m_TreeBank, &TreeBank::CurrentChanged,       this, &TreeBar::OnCurrentChanged);
    disconnect(this,       &QToolBar::orientationChanged,   this, &TreeBar::CollectNodes);
    m_LayerList.clear();
    m_Scene->clear();
    StopAutoUpdateTimer();
}

void TreeBar::enterEvent(QEvent *ev){
    QToolBar::enterEvent(ev);
}

void TreeBar::leaveEvent(QEvent *ev){
    QToolBar::leaveEvent(ev);
}

void TreeBar::mouseMoveEvent(QMouseEvent *ev){
    QToolBar::mouseMoveEvent(ev);
}

void TreeBar::mousePressEvent(QMouseEvent *ev){
    QToolBar::mousePressEvent(ev);
}

void TreeBar::mouseReleaseEvent(QMouseEvent *ev){
    QToolBar::mouseReleaseEvent(ev);
}

LayerItem::LayerItem(TreeBank *tb, TreeBar *bar, Node *nd, Node *pnd, QGraphicsItem *parent)
    : QGraphicsObject(parent)
{
    m_TreeBank = tb;
    m_TreeBar = bar;
    m_Node = nd;
    m_FocusedNode = 0;
    m_Nest = 0;
    m_ScrollUpTimerId = 0;
    m_ScrollDownTimerId = 0;
    m_CurrentScroll = 0.0;
    m_TargetScroll = 0.0;
    m_NodeItems = QList<NodeItem*>();

    new PlusButton(tb, bar, this);
    new LayerScroller(tb, bar, this);

    m_PrevScrollButton = 0;
    m_NextScrollButton = 0;

    m_InsertPosition = new InsertPosition(tb, bar, this);

    m_Line = new QGraphicsLineItem(this);
    if(Application::EnableTransparentBar()){
        m_Line->setPen(Qt::NoPen);
    } else {
        static const QPen p = QPen(QColor(150, 150, 150, 255));
        m_Line->setPen(p);
    }
    m_Line->setZValue(BORDEF_LINE_LAYER);

    m_Animation = new QPropertyAnimation(this, "scroll");
    connect(m_Animation, &QPropertyAnimation::finished,
            this, &LayerItem::ResetTargetScroll);

    class DummyNode : public ViewNode{
        bool IsDummy() const Q_DECL_OVERRIDE { return true;}
    };

    m_DummyNode = new DummyNode();
    if(!nd) m_Node = m_DummyNode;
    if(pnd) m_DummyNode->SetParent(pnd);

    setAcceptDrops(true);
}

LayerItem::~LayerItem(){
    m_DummyNode->Delete();
}

void LayerItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget){
    Q_UNUSED(painter); Q_UNUSED(option); Q_UNUSED(widget);
    //painter->setPen(QPen(QColor(0, 0, 0, 255)));
    //painter->setBrush(Qt::NoBrush);
    //QRectF rect = boundingRect();
    //painter->drawRect(rect.left(), rect.top(), rect.width()-1, rect.height()-1);
}

QRectF LayerItem::boundingRect() const {
    if(!scene()) return QRectF();
    switch(m_TreeBar->orientation()){
    case Qt::Horizontal:{
        int height = m_TreeBar->GetHorizontalNodeHeight() + 3;
        return QRectF(0, Index() * height,
                      scene()->sceneRect().width(), height);
    }
    case Qt::Vertical:
        return QRectF(QPointF(), scene()->sceneRect().size());
    }
    return QRectF();
}

int LayerItem::Index() const {
    return m_TreeBar->GetLayerList().indexOf(const_cast<LayerItem* const>(this));
}

int LayerItem::GetNest() const {
    return m_Nest;
}

void LayerItem::SetNest(int nest){
    m_Nest = nest;
}

QPropertyAnimation *LayerItem::GetAnimation() const {
    return m_Animation;
}

bool LayerItem::IsLocked() const {
    return m_Animation->state() == QAbstractAnimation::Running;
}

qreal LayerItem::MaxScroll() const {
    if(!scene()) return 0.0;
    if(Application::EnableTransparentBar()){
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            return m_TreeBar->GetHorizontalNodeWidth() * (m_NodeItems.length() - 0.5)
                - scene()->sceneRect().width() / 2.0 + FRINGE_BUTTON_SIZE;
        case Qt::Vertical:
            return m_TreeBar->GetVerticalNodeHeight() * (m_NodeItems.length() - 0.5)
                - scene()->sceneRect().height() / 2.0 + FRINGE_BUTTON_SIZE;
        }
    }
    switch(m_TreeBar->orientation()){
    case Qt::Horizontal:
        return m_TreeBar->GetHorizontalNodeWidth() * m_NodeItems.length()
            - scene()->sceneRect().width() + FRINGE_BUTTON_SIZE * 2;
    case Qt::Vertical:
        return m_TreeBar->GetVerticalNodeHeight() * m_NodeItems.length()
            - scene()->sceneRect().height() + FRINGE_BUTTON_SIZE * 2;
    }
    return 0.0;
}

qreal LayerItem::MinScroll() const {
    if(!scene()) return 0.0;
    if(Application::EnableTransparentBar()){
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            return m_TreeBar->GetHorizontalNodeWidth() * 0.5
                - scene()->sceneRect().width() / 2.0 + FRINGE_BUTTON_SIZE;
        case Qt::Vertical:
            return m_TreeBar->GetVerticalNodeHeight() * 0.5
                - scene()->sceneRect().height() / 2.0 + FRINGE_BUTTON_SIZE;
        }
    }
    return 0.0;
}

qreal LayerItem::GetScroll() const {
    return m_CurrentScroll;
}

void LayerItem::SetScroll(qreal scroll){
    if(!Cramp(scroll, MinScroll(), MaxScroll())) return;
    if(m_CurrentScroll == scroll){ OnScrolled(); return;}
    m_CurrentScroll = scroll;
    OnScrolled();
    update();
}

void LayerItem::Scroll(qreal delta){
    if(TreeBar::EnableAnimation()
       && m_TreeBar->GetView()->MouseEventSource() != Qt::MouseEventSynthesizedBySystem){

        qreal orig = m_TargetScroll;
        m_TargetScroll += delta;

        if(!Cramp(m_TargetScroll, MinScroll(), MaxScroll())) return;

        if(m_TargetScroll == m_CurrentScroll) return;

        if(m_Animation->state() == QAbstractAnimation::Running &&
           m_Animation->easingCurve() == QEasingCurve::OutCubic){

            if(m_TargetScroll == orig) return;

        } else {
            m_Animation->setEasingCurve(QEasingCurve::OutCubic);
            m_Animation->setDuration(366);
        }
        m_Animation->setStartValue(m_CurrentScroll);
        m_Animation->setEndValue(m_TargetScroll);
        m_Animation->start();
        m_Animation->setCurrentTime(16);
    } else {
        m_Animation->stop();
        SetScroll(m_CurrentScroll + delta);
        ResetTargetScroll();
    }
}

void LayerItem::ScrollForDelete(int count){
    int size =
        m_TreeBar->orientation() == Qt::Horizontal ? m_TreeBar->GetHorizontalNodeWidth() :
        m_TreeBar->orientation() == Qt::Vertical   ? m_TreeBar->GetVerticalNodeHeight() :
        0;
    if(GetScroll() > MaxScroll() - size * count){
        ResetTargetScroll();
        Scroll(MaxScroll() - GetScroll() - size * count);
    }
}

void LayerItem::LockWhileAnimating(){
    // lock node items by scrolling to same position.
    if(m_Animation->state() != QAbstractAnimation::Running){
        qreal scroll = GetScroll();
        m_TargetScroll = scroll;
        m_Animation->setEasingCurve(QEasingCurve::OutCubic);
        m_Animation->setDuration(366);
        m_Animation->setStartValue(scroll);
        m_Animation->setEndValue(scroll);
        m_Animation->start();
    }
}

void LayerItem::ResetTargetScroll(){
    m_TargetScroll = m_CurrentScroll;
}

void LayerItem::AutoScrollDown(){
    if(MaxScroll() <= MinScroll() || GetScroll() == MaxScroll() ||
       (m_Animation->state() == QAbstractAnimation::Running &&
        m_Animation->easingCurve() == QEasingCurve::Linear))
        return;

    m_Animation->setEasingCurve(QEasingCurve::Linear);
    m_Animation->setStartValue(GetScroll());
    m_Animation->setEndValue(MaxScroll());
    m_Animation->setDuration((MaxScroll() - GetScroll()) * 2);
    m_Animation->start();
}

void LayerItem::AutoScrollUp(){
    if(MaxScroll() <= MinScroll() || GetScroll() == MinScroll() ||
       (m_Animation->state() == QAbstractAnimation::Running &&
        m_Animation->easingCurve() == QEasingCurve::Linear))
        return;

    m_Animation->setEasingCurve(QEasingCurve::Linear);
    m_Animation->setStartValue(GetScroll());
    m_Animation->setEndValue(MinScroll());
    m_Animation->setDuration((GetScroll() - MinScroll()) * 2);
    m_Animation->start();
}

void LayerItem::AutoScrollStop(){
    m_Animation->stop();
    ResetTargetScroll();
    StopScrollDownTimer();
    StopScrollUpTimer();
}

void LayerItem::AutoScrollStopOrScroll(qreal delta){
    if(delta > 0)
        StopScrollDownTimer();
    else
        StopScrollUpTimer();

    if(m_Animation->state() == QAbstractAnimation::Running){
        m_Animation->stop();
        ResetTargetScroll();
    }
    else Scroll(delta);
}

void LayerItem::StartScrollDownTimer(){
    if(!m_ScrollDownTimerId){
        m_ScrollDownTimerId = startTimer(200);
    }
}

void LayerItem::StartScrollUpTimer(){
    if(!m_ScrollUpTimerId){
        m_ScrollUpTimerId = startTimer(200);
    }
}

void LayerItem::StopScrollDownTimer(){
    if(m_ScrollDownTimerId){
        killTimer(m_ScrollDownTimerId);
        m_ScrollDownTimerId = 0;
    }
}

void LayerItem::StopScrollUpTimer(){
    if(m_ScrollUpTimerId){
        killTimer(m_ScrollUpTimerId);
        m_ScrollUpTimerId = 0;
    }
}

void LayerItem::Adjust(qreal scroll){
    int i = 0;
    if(m_Node && !m_Node->IsDummy()){
        for(; i < m_NodeItems.length(); i++){
            if(m_NodeItems[i]->GetNode() == m_Node) break;
        }
    }
    switch(m_TreeBar->orientation()){
    case Qt::Horizontal:{
        int width = boundingRect().width();
        int height = m_TreeBar->GetHorizontalNodeHeight();

        if(m_Animation->state() != QAbstractAnimation::Running){

            foreach(NodeItem *item, m_NodeItems){
                QRectF rect = item->GetRect();
                rect.moveTop(Index() * (height + 3) + 1);
                rect.setHeight(height);
                item->SetRect(rect);
                item->setSelected(false);
            }

            if(scroll >= MinScroll() && scroll <= MaxScroll())
                SetScroll(scroll);
            else
                SetScroll(m_TreeBar->GetHorizontalNodeWidth() * (i + 0.5)
                          - scene()->sceneRect().width() / 2.0 + FRINGE_BUTTON_SIZE);
            ResetTargetScroll();
        }

        SetLine(0.0,   (height + 3) * (Index() + 1) - 2,
                width, (height + 3) * (Index() + 1) - 2);
        break;
    }
    case Qt::Vertical:{
        int width = m_TreeBar->GetVerticalNodeWidth();
        int height = boundingRect().height();

        if(m_Animation->state() != QAbstractAnimation::Running){

            foreach(NodeItem *item, m_NodeItems){
                QRectF rect = item->GetRect();
                rect.setRight(width + 1);
                item->SetRect(rect);
                item->setSelected(false);
            }

            if(scroll >= MinScroll() && scroll <= MaxScroll())
                SetScroll(scroll);
            else
                SetScroll(m_TreeBar->GetVerticalNodeHeight() * (i + 0.5)
                          - scene()->sceneRect().height() / 2.0 + FRINGE_BUTTON_SIZE);
            ResetTargetScroll();
        }

        SetLine(width + 1, 0,
                width + 1, height);
        break;
    }
    }
}

void LayerItem::OnScrolled(){
    switch(m_TreeBar->orientation()){
    case Qt::Horizontal:{
        if(MaxScroll() > MinScroll() && GetScroll() > MinScroll()){
            if(!m_PrevScrollButton)
                m_PrevScrollButton = new LeftScrollButton(m_TreeBank, m_TreeBar, this);
        } else if(m_PrevScrollButton){
            scene()->removeItem(m_PrevScrollButton);
            m_PrevScrollButton = 0;
        }
        if(MaxScroll() > MinScroll() && GetScroll() < MaxScroll()){
            if(!m_NextScrollButton)
                m_NextScrollButton = new RightScrollButton(m_TreeBank, m_TreeBar, this);
        } else if(m_NextScrollButton){
            scene()->removeItem(m_NextScrollButton);
            m_NextScrollButton = 0;
        }
        break;
    }
    case Qt::Vertical:{
        if(MaxScroll() > MinScroll() && GetScroll() > MinScroll()){
            if(!m_PrevScrollButton)
                m_PrevScrollButton = new UpScrollButton(m_TreeBank, m_TreeBar, this);
        } else if(m_PrevScrollButton){
            scene()->removeItem(m_PrevScrollButton);
            m_PrevScrollButton = 0;
        }
        if(MaxScroll() > MinScroll() && GetScroll() < MaxScroll()){
            if(!m_NextScrollButton)
                m_NextScrollButton = new DownScrollButton(m_TreeBank, m_TreeBar, this);
        } else if(m_NextScrollButton){
            scene()->removeItem(m_NextScrollButton);
            m_NextScrollButton = 0;
        }
        break;
    }
    }
    CorrectOrder();
}

NodeItem *LayerItem::GetFocusedNode() const {
    return m_FocusedNode;
}

void LayerItem::SetFocusedNode(NodeItem *item){
    m_FocusedNode = item;
}

void LayerItem::CorrectOrder(){
    if(!m_FocusedNode || m_NodeItems.length() < 2) return;

    int i = m_NodeItems.indexOf(m_FocusedNode);
    if(i == -1) return;
    bool moved = false;

    const int halfWidth  = m_TreeBar->GetHorizontalNodeWidth() / 2.5;
    const int halfHeight = m_TreeBar->GetVerticalNodeHeight()  / 2.5;

    switch(m_TreeBar->orientation()){
    case Qt::Horizontal:{
        while(i+1 < m_NodeItems.length() &&
              halfWidth
              >= (m_NodeItems[i+1]->ScheduledPosition().x() -
                  m_NodeItems[i]->ScheduledPosition().x())){
            moved = true;
            SwapWithPrev(i+1);
            i++;
        }
        if(moved) break;
        while(i > 0 &&
              halfWidth
              >= (m_NodeItems[i]->ScheduledPosition().x() -
                  m_NodeItems[i-1]->ScheduledPosition().x())){
            SwapWithNext(i-1);
            i--;
        }
        break;
    }
    case Qt::Vertical:{
        while(i+1 < m_NodeItems.length() &&
              halfHeight
              >= (m_NodeItems[i+1]->ScheduledPosition().y() -
                  m_NodeItems[i]->ScheduledPosition().y())){
            moved = true;

            int nest = m_NodeItems[i+1]->GetNode()->IsDirectory()
                ? m_NodeItems[i+1]->GetNest()+1
                : m_NodeItems[i+1]->GetNest();

            if(m_NodeItems[i]->ScheduledPosition().y() != DBL_MAX &&
               m_NodeItems[i]->GetNest() != nest){

                m_NodeItems[i]->SetNest(nest);
                m_NodeItems[i]->OnNestChanged();
            }
            SwapWithPrev(i+1);
            i++;
        }
        if(moved) break;
        while(i > 0 &&
              halfHeight
              >= (m_NodeItems[i]->ScheduledPosition().y() -
                  m_NodeItems[i-1]->ScheduledPosition().y())){

            int nest = m_NodeItems[i-1]->GetNest();

            if(m_NodeItems[i]->ScheduledPosition().y() != DBL_MAX &&
               m_NodeItems[i]->GetNest() != nest){

                m_NodeItems[i]->SetNest(nest);
                m_NodeItems[i]->OnNestChanged();
            }
            SwapWithNext(i-1);
            i--;
        }
        break;
    }
    }
}

void LayerItem::SetNode(Node *nd){
    m_Node = nd;
}

Node *LayerItem::GetNode() const {
    return (!m_Node || m_Node->IsDummy()) ? 0 : m_Node;
}

void LayerItem::SetLine(qreal x1, qreal y1, qreal x2, qreal y2){
    m_Line->setLine(x1, y1, x2, y2);
}

void LayerItem::ApplyChildrenOrder(){
    Node *parent = 0;
    NodeList list;
    switch(m_TreeBar->orientation()){
    case Qt::Horizontal:{
        foreach(NodeItem *item, m_NodeItems){
            list << item->GetNode();
        }
        parent = m_Node->GetParent();
        break;
    }
    case Qt::Vertical:{
        Node *nd = 0;
        int i = 0;
        for(; i < m_NodeItems.length(); i++){
            if(m_NodeItems[i]->isSelected()){
                nd = m_NodeItems[i]->GetNode();
                break;
            }
        }
        if(!nd) return;
        Node *above = 0;
        Node *below = 0;
        if(i > 0){
            above = m_NodeItems[i-1]->GetNode();
        }
        if(i < m_NodeItems.length() - 1){
            below = m_NodeItems[i+1]->GetNode();
        }
        if(above && m_NodeItems[i]->GetNest() == m_NodeItems[i-1]->GetNest()){
            list = above->GetSiblings();
            if(list.contains(nd)) list.removeOne(nd);
            list.insert(list.indexOf(above) + 1, nd);
            parent = above->GetParent();
            break;
        }
        if(below && m_NodeItems[i]->GetNest() == m_NodeItems[i+1]->GetNest()){
            list = below->GetSiblings();
            if(list.contains(nd)) list.removeOne(nd);
            list.insert(list.indexOf(below), nd);
            parent = below->GetParent();
            break;
        }
        if(above && m_NodeItems[i-1]->GetNode()->IsDirectory() &&
           m_NodeItems[i]->GetNest() == m_NodeItems[i-1]->GetNest()+1){
            list = above->GetChildren();
            if(list.contains(nd)) list.removeOne(nd);
            list.prepend(nd);
            parent = above;
            break;
        }
        m_NodeItems[i]->setSelected(false);
        m_NodeItems[i]->setPos(QPointF());
        return;
    }
    }
    if(!m_TreeBank->SetChildrenOrder(parent, list)){
        m_TreeBar->CollectNodes(); // reset.
    }
}

void LayerItem::TransferNodeItem(NodeItem *item, LayerItem *other){
    int index = m_NodeItems.indexOf(item);
    int len = m_NodeItems.length();
    for(int i = 0; i < len; i++){
        m_NodeItems[i]->SetHoveredWithItem(false);
        if(i > index) SwapWithPrev(i);
    }
    other->m_FocusedNode = item;
    m_FocusedNode = 0;
    m_NodeItems.removeOne(item);
    item->setParentItem(other);
    other->m_NodeItems.append(item);
    QRectF rect = item->GetRect();
    rect.moveTop(other->Index() * (m_TreeBar->GetHorizontalNodeHeight() + 3) + 1);
    item->SetRect(rect);
}

NodeItem *LayerItem::CreateNodeItem(Node *nd, int i, int j, int nest, int size){
    NodeItem *item = new NodeItem(m_TreeBank, m_TreeBar, nd, this);
    switch(m_TreeBar->orientation()){
    case Qt::Horizontal:
        item->SetRect(QRectF(j * m_TreeBar->GetHorizontalNodeWidth() + FRINGE_BUTTON_SIZE,
                             i * (size + 3) + 1,
                             m_TreeBar->GetHorizontalNodeWidth(),
                             size));
        break;
    case Qt::Vertical:
        item->SetNest(nest);
        item->SetRect(QRectF(nest * 20 + 1,
                             i * m_TreeBar->GetVerticalNodeHeight() + FRINGE_BUTTON_SIZE,
                             size - nest * 20,
                             m_TreeBar->GetVerticalNodeHeight()));
        break;
    }
    AppendToNodeItems(item);
    return item;
}

QList<NodeItem*> &LayerItem::GetNodeItems(){
    return m_NodeItems;
}

void LayerItem::AppendToNodeItems(NodeItem *item){
    m_NodeItems.append(item);
}

void LayerItem::PrependToNodeItems(NodeItem *item){
    m_NodeItems.prepend(item);
}

void LayerItem::RemoveFromNodeItems(NodeItem *item){
    m_NodeItems.removeOne(item);
}

void LayerItem::SwapWithNext(int index){
    m_NodeItems[index]->Slide(1);
    m_NodeItems.swap(index, index+1);
}

void LayerItem::SwapWithPrev(int index){
    m_NodeItems[index]->Slide(-1);
    m_NodeItems.swap(index-1, index);
}

QMenu *LayerItem::LayerMenu(){
    QMenu *menu = new QMenu(m_TreeBar);

    QAction *newViewNode = new QAction(menu);
    newViewNode->setText(QObject::tr("NewViewNode"));
    newViewNode->connect(newViewNode, &QAction::triggered, this, &LayerItem::NewViewNode);
    menu->addAction(newViewNode);

    QAction *cloneViewNode = new QAction(menu);
    cloneViewNode->setText(QObject::tr("CloneViewNode"));
    cloneViewNode->connect(cloneViewNode, &QAction::triggered, this, &LayerItem::CloneViewNode);
    menu->addAction(cloneViewNode);

    menu->addSeparator();

    QAction *makeDirectory = new QAction(menu);
    makeDirectory->setText(QObject::tr("MakeDirectory"));
    makeDirectory->connect(makeDirectory, &QAction::triggered, this, &LayerItem::MakeDirectory);
    menu->addAction(makeDirectory);
    QAction *makeDirectoryWithSelected = new QAction(menu);
    makeDirectoryWithSelected->setText(QObject::tr("MakeDirectoryWithSelectedNode"));
    makeDirectoryWithSelected->connect
        (makeDirectoryWithSelected, &QAction::triggered,
         this, &LayerItem::MakeDirectoryWithSelectedNode);
    menu->addAction(makeDirectoryWithSelected);
    QAction *makeDirectoryWithSameDomain = new QAction(menu);
    makeDirectoryWithSameDomain->setText(QObject::tr("MakeDirectoryWithSameDomainNode"));
    makeDirectoryWithSameDomain->connect
        (makeDirectoryWithSameDomain, &QAction::triggered,
         this, &LayerItem::MakeDirectoryWithSameDomainNode);
    menu->addAction(makeDirectoryWithSameDomain);

    menu->addSeparator();

    menu->addAction(m_TreeBank->Action(TreeBank::_ToggleMenuBar));
    menu->addAction(m_TreeBank->Action(TreeBank::_ToggleTreeBar));
    menu->addAction(m_TreeBank->Action(TreeBank::_ToggleToolBar));

    menu->addSeparator();

    m_TreeBar->AddTreeBarMenu(menu);

    return menu;
}

QMenu *LayerItem::MakeNodeMenu(){
    QMenu *menu = new QMenu(m_TreeBar);
    menu->setToolTipsVisible(true);

    QAction *newViewNode = new QAction(menu);
    newViewNode->setText(QObject::tr("NewViewNode"));
    newViewNode->connect(newViewNode, &QAction::triggered, this, &LayerItem::NewViewNode);
    menu->addAction(newViewNode);

    QAction *cloneViewNode = new QAction(menu);
    cloneViewNode->setText(QObject::tr("CloneViewNode"));
    cloneViewNode->connect(cloneViewNode, &QAction::triggered, this, &LayerItem::CloneViewNode);
    menu->addAction(cloneViewNode);

    NodeList trash = TreeBank::GetTrashRoot()->GetChildren();

    QMenu *restoreMenu = menu;

    if(trash.length())
        restoreMenu->addSeparator();

    TreeBank *tb = m_TreeBank;
    Node *nd = GetNode();
    Node *pnd = m_DummyNode->GetParent();

    int max = 20;
    int i = 0;
    int j = 0;

    for(; j < 10; j++, max+=20){
        for(; i < qMin(max, trash.length()); i++){
            ViewNode *t = trash[i]->ToViewNode();
            QAction *restore = new QAction(restoreMenu);
            QString title = t->ReadableTitle();

            restore->setToolTip(title);

            if(title.length() > 25)
                title = title.left(25) + QStringLiteral("...");
            restore->setText(title);

            restore->connect
                (restore, &QAction::triggered,
                 [t, nd, pnd, tb](){
                    // both function emits TreeStructureChanged for current.
                    if(nd)
                        tb->MoveNode(t, nd->GetParent(), nd->Index() + 1);
                    else if(pnd)
                        tb->MoveNode(t, pnd);
                    else
                        tb->MoveNode(t, TreeBank::GetViewRoot());
                    tb->SetCurrent(t);
                });
            restoreMenu->addAction(restore);
        }
        if(i >= trash.length()) break;
        if(j < 9){
            QMenu *child = new QMenu(QObject::tr("More"), restoreMenu);
            restoreMenu->addMenu(child);
            restoreMenu = child;
        }
    }
    QAction *displayTrashTree = new QAction(restoreMenu);
    displayTrashTree->setText(QObject::tr("DisplayTrashTree"));
    displayTrashTree->connect(displayTrashTree, &QAction::triggered,
                              this, &LayerItem::DisplayTrashTree);
    restoreMenu->addAction(displayTrashTree);

    return menu;
}

void LayerItem::NewViewNode(){
    if(Node *nd = GetNode())
        m_TreeBank->NewViewNode(nd->ToViewNode());
    else if(Node *nd = m_DummyNode->GetParent())
        m_TreeBank->OpenOnSuitableNode(BLANK_URL, true, nd->ToViewNode());
    else
        m_TreeBank->OpenOnSuitableNode(BLANK_URL, true);
}

void LayerItem::CloneViewNode(){
    if(Node *nd = GetNode())
        m_TreeBank->CloneViewNode(nd->ToViewNode());
}

void LayerItem::MakeDirectory(){
    if(Node *nd = GetNode())
        m_TreeBank->MakeSiblingDirectory(nd->ToViewNode());
    else if(Node *nd = m_DummyNode->GetParent())
        m_TreeBank->MakeChildDirectory(nd->ToViewNode());
    else
        m_TreeBank->MakeChildDirectory(TreeBank::GetViewRoot());
}

void LayerItem::MakeDirectoryWithSelectedNode(){
    if(Node *nd = GetNode()){
        Node *parent = m_TreeBank->MakeSiblingDirectory(nd->ToViewNode());
        m_TreeBank->SetChildrenOrder(parent, NodeList() << nd);
    }
}

void LayerItem::MakeDirectoryWithSameDomainNode(){
    Node *nd = GetNode();
    Node *pnd = m_DummyNode->GetParent();
    ViewNode *parent =
        nd ? nd->GetParent()->ToViewNode() :
        pnd ? pnd->ToViewNode() : 0;
    if(!parent) return;

    QMap<QString, NodeList> groups;
    foreach(Node *n, parent->GetChildren()){
        if(!n->IsDirectory()) groups[n->GetUrl().host()] << n;
    }
    if(groups.count() <= 1) return;
    foreach(QString domain, groups.keys()){
        Node *directory = parent->MakeChild();
        directory->SetTitle(domain);
        m_TreeBank->SetChildrenOrder(directory, groups[domain]);
    }
}

void LayerItem::ToggleEnableAnimation(){
    TreeBar::ToggleEnableAnimation();
}

void LayerItem::ToggleEnableCloseButton(){
    TreeBar::ToggleEnableCloseButton();
}

void LayerItem::ToggleEnableCloneButton(){
    TreeBar::ToggleEnableCloneButton();
}

void LayerItem::DisplayTrashTree(){
    m_TreeBank->DisplayTrashTree();
}

void LayerItem::timerEvent(QTimerEvent *ev){
    QGraphicsObject::timerEvent(ev);
    if(ev->timerId() == m_ScrollDownTimerId){
        killTimer(m_ScrollDownTimerId);
        m_ScrollDownTimerId = 0;
        AutoScrollDown();
    }
    if(ev->timerId() == m_ScrollUpTimerId){
        killTimer(m_ScrollUpTimerId);
        m_ScrollUpTimerId = 0;
        AutoScrollUp();
    }
}

void LayerItem::dragEnterEvent(QGraphicsSceneDragDropEvent *ev){
    QGraphicsObject::dragEnterEvent(ev);
}

void LayerItem::dropEvent(QGraphicsSceneDragDropEvent *ev){
    InsertPosition *position = static_cast<InsertPosition*>(m_InsertPosition);
    if(NodeItem *item = position->GetTarget()){
        if(item->boundingRect().intersects(scene()->sceneRect())){
            if(const QMimeData *mime = ev->mimeData()){
                QList<QUrl> urls = Page::MimeDataToUrls(mime, ev->source());
                Node *nd = item->GetNode();
                ViewNode *parent = nd->GetParent()->ToViewNode();
                int index = nd->Index();

                if(position->GetWhere() == InsertPosition::RightOfTarget){
                    index++;
                }
                if(!urls.isEmpty()){
                    m_TreeBank->OpenOnSuitableNode(urls, true, parent, index);
                    ev->setAccepted(true);
                }
            }
        }
    }
    position->SetTarget(0);
    QGraphicsObject::dropEvent(ev);
}

void LayerItem::dragMoveEvent(QGraphicsSceneDragDropEvent *ev){
    InsertPosition *position = static_cast<InsertPosition*>(m_InsertPosition);
    NodeItem *node = 0;
    foreach(QGraphicsItem *item, scene()->items(ev->scenePos())){
        node = dynamic_cast<NodeItem*>(item);
        if(node) break;
    }
    if(!node && !m_NodeItems.isEmpty()){
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            if(ev->scenePos().x() < m_NodeItems.first()->boundingRect().center().x())
                node = m_NodeItems.first();
            else if(ev->scenePos().x() > m_NodeItems.last()->boundingRect().center().x())
                node = m_NodeItems.last();
            break;
        case Qt::Vertical:
            if(ev->scenePos().y() < m_NodeItems.first()->boundingRect().center().y())
                node = m_NodeItems.first();
            else if(ev->scenePos().y() > m_NodeItems.last()->boundingRect().center().y())
                node = m_NodeItems.last();
            break;
        }
    }
    if(node){
        InsertPosition::Where where = InsertPosition::NoWhere;
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:

            if(ev->scenePos().x() < node->boundingRect().center().x())
                where = InsertPosition::LeftOfTarget;
            else where = InsertPosition::RightOfTarget;

            if(ev->scenePos().x() < scene()->sceneRect().left() + FRINGE_BUTTON_SIZE)
                AutoScrollUp();
            else if(ev->scenePos().x() > scene()->sceneRect().right() - FRINGE_BUTTON_SIZE)
                AutoScrollDown();
            else
                AutoScrollStop();
            break;

        case Qt::Vertical:

            if(ev->scenePos().y() < node->boundingRect().center().y())
                where = InsertPosition::LeftOfTarget;
            else where = InsertPosition::RightOfTarget;

            if(ev->scenePos().y() < scene()->sceneRect().top() + FRINGE_BUTTON_SIZE)
                AutoScrollUp();
            else if(ev->scenePos().y() > scene()->sceneRect().bottom() - FRINGE_BUTTON_SIZE)
                AutoScrollDown();
            else
                AutoScrollStop();
            break;
        }
        position->SetTarget(node, where);
        QGraphicsObject::dragMoveEvent(ev);
        ev->setAccepted(true);
    } else {
        position->SetTarget(0);
        QGraphicsObject::dragMoveEvent(ev);
    }
}

void LayerItem::dragLeaveEvent(QGraphicsSceneDragDropEvent *ev){
    AutoScrollStop();
    static_cast<InsertPosition*>(m_InsertPosition)->SetTarget(0);
    QGraphicsObject::dragLeaveEvent(ev);
}

void LayerItem::mousePressEvent(QGraphicsSceneMouseEvent *ev){
    Q_UNUSED(ev);
    //QGraphicsObject::mousePressEvent(ev);
}

void LayerItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *ev){
    //QGraphicsObject::mouseReleaseEvent(ev);
    if(ev->button() == Qt::RightButton &&
       (ev->buttonDownScreenPos(Qt::RightButton) - ev->screenPos()).manhattanLength() < 4){
        QMenu *menu = LayerMenu();
        menu->exec(ev->screenPos());
        delete menu;
    }
}

void LayerItem::mouseMoveEvent(QGraphicsSceneMouseEvent *ev){
    QGraphicsObject::mouseMoveEvent(ev);
}

void LayerItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev){
    QGraphicsObject::mouseDoubleClickEvent(ev);
}

void LayerItem::hoverEnterEvent(QGraphicsSceneHoverEvent *ev){
    QGraphicsObject::hoverEnterEvent(ev);
}

void LayerItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *ev){
    QGraphicsObject::hoverLeaveEvent(ev);
}

void LayerItem::hoverMoveEvent(QGraphicsSceneHoverEvent *ev){
    QGraphicsObject::hoverMoveEvent(ev);
}

void LayerItem::wheelEvent(QGraphicsSceneWheelEvent *ev){

    bool up = ev->delta() > 0;

    if(m_TreeBar->GetView()->MouseEventSource() != Qt::MouseEventSynthesizedBySystem){

        if(Application::HasCtrlModifier(ev)){
            switch(m_TreeBar->orientation()){
            case Qt::Horizontal:{
                int width = m_TreeBar->GetHorizontalNodeWidth();
                if(up) width += 5;
                else   width -= 5;
                Cramp(width,
                      TREEBAR_HORIZONTAL_NODE_MINIMUM_WIDTH,
                      TREEBAR_HORIZONTAL_NODE_MAXIMUM_WIDTH);
                m_TreeBar->SetHorizontalNodeWidth(width);
                break;
            }
            case Qt::Vertical:{
                int height = m_TreeBar->GetVerticalNodeHeight();
                if(up) height += 5;
                else   height -= 5;
                Cramp(height,
                      TREEBAR_VERTICAL_NODE_MINIMUM_HEIGHT,
                      TREEBAR_VERTICAL_NODE_MAXIMUM_HEIGHT);
                m_TreeBar->SetVerticalNodeHeight(height);
                break;
            }
            }
            return;

        } else if(TreeBar::ScrollToSwitchNode()){
            NodeList siblings = m_Node->GetSiblings();
            int index = siblings.indexOf(m_Node);
            if(up){
                if(index >= 1)
                    m_TreeBank->SetCurrent(siblings[index-1]);
            } else {
                if(index < siblings.length()-1)
                    m_TreeBank->SetCurrent(siblings[index+1]);
            }
            return;
        }
    }

    if(!TreeBar::EnableAnimation()){

        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            if(up) Scroll(-m_TreeBar->GetHorizontalNodeWidth());
            else   Scroll(m_TreeBar->GetHorizontalNodeWidth());
            break;
        case Qt::Vertical:
            if(up) Scroll(-m_TreeBar->GetVerticalNodeHeight() * 3.0);
            else   Scroll(m_TreeBar->GetVerticalNodeHeight() * 3.0);
            break;
        }
    } else {

        Scroll(-ev->delta());
    }
}

NodeItem::NodeItem(TreeBank *tb, TreeBar *bar, Node *nd, QGraphicsItem *parent)
    : QGraphicsObject(parent)
{
    m_TreeBank = tb;
    m_TreeBar = bar;
    m_Node = nd;
    m_Nest = 0;
    m_IsFocused = static_cast<LayerItem*>(parent)->GetNode() == nd;
    m_IsHovered = false;
    m_HoveredTimerId = 0;
    m_ButtonState = NotHovered;
    m_TargetPosition = QPoint();
    m_Animation = new QPropertyAnimation(this, "rect");
    m_Animation->setDuration(334);
    m_Animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_Animation, &QPropertyAnimation::finished,
            this, &NodeItem::ResetTargetPosition);

    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    if(m_IsFocused)
        setZValue(FOCUSED_NODE_LAYER);
    else
        setZValue(NORMAL_NODE_LAYER);

    if(!nd) return;

    QString title = nd->ReadableTitle();

    HistNode *hn = nd->ToHistNode();

    if(!hn && nd->GetPartner())
        hn = nd->GetPartner()->ToHistNode();

    if(hn && !hn->GetImageFileName().isEmpty())
        title = QStringLiteral("<center><img src=\"%1\"><br>%2</center>")
            .arg(Application::ThumbnailDirectory() + hn->GetImageFileName())
            .arg(title);

    setToolTip(title);
}

NodeItem::~NodeItem(){}

void NodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget){
    Q_UNUSED(option); Q_UNUSED(widget);

    QRectF bound = boundingRect();
    QRectF realRect = bound.translated(pos());

    if(!scene() || !realRect.intersects(scene()->sceneRect()) ||
       (isSelected() && m_TreeBar->TabWindowVisible())) return;

    painter->save();

    View *view = m_Node->GetView();
    bool isDir = m_Node->IsDirectory();
    bool contrast = Application::EnableTransparentBar() && m_IsFocused;

    QRect title_rect = bound.toRect();
    title_rect.setTop(qMax(title_rect.top() + 2,
                           title_rect.bottom()
                           - m_TreeBar->ScaleByDevice(21)));
    title_rect.setBottom(title_rect.bottom() - 2);
    title_rect.setLeft(title_rect.left() + 4);
    title_rect.setRight(title_rect.right() - 4);

    if(m_IsHovered){
        if(TreeBar::EnableCloseButton())
            title_rect.setRight(title_rect.right() - 18);
        if(TreeBar::EnableCloneButton())
            title_rect.setRight(title_rect.right() - 18);
    }

    bool muted = false;
    bool audible = false;
    if(view){
        muted = view->IsAudioMuted();
        audible = view->RecentlyAudible();
        /*
        if(muted || audible)
            title_rect.setRight(title_rect.right() - 18);
        */
    }

    {   // base.
        if(Application::EnableTransparentBar()){
            static const QBrush hnb = QBrush(QColor(  0,   0,   0, 205));
            static const QBrush  nb = QBrush(QColor(  0,   0,   0, 175));
            static const QBrush htb = QBrush(QColor(255, 255, 255, 215));
            static const QBrush  tb = QBrush(QColor(255, 255, 255, 185));
            painter->setBrush(m_IsFocused
                              ? (m_IsHovered ? hnb : nb)
                              : (m_IsHovered ? htb : tb));
            painter->setPen(Qt::NoPen);
        } else {
            static const QBrush b = QBrush(QColor(240, 240, 240, 255));
            painter->setBrush(b);
            painter->setPen(Qt::NoPen);
        }
        QRectF rect = bound;
        if(!Application::EnableTransparentBar()){
            switch(m_TreeBar->orientation()){
            case Qt::Horizontal: rect.setBottom(rect.bottom() + 1); break;
            case Qt::Vertical:   rect.setRight(rect.right() + 1);   break;
            }
        }
        painter->drawRect(rect);
    }

    if(!title_rect.isValid()){ painter->restore(); return;}

    {   // image.
        QImage image = m_Node->VisibleImage();

        QRectF image_rect = bound;
        image_rect.setLeft(image_rect.left() + 3);
        image_rect.setRight(image_rect.right() - 3);
        image_rect.setTop(image_rect.top() + 3);
        image_rect.setBottom(image_rect.bottom() - 3
                             - m_TreeBar->ScaleByDevice(22));

        if(!image_rect.isValid()){
            // nothing to do.
        } else if(!image.isNull()){
            QRectF source = QRectF(0, 0,
                                   image.width(),
                                   image.width() * image_rect.height() / image_rect.width());
            painter->drawImage(image_rect, image, source);
        } else {
            static const QBrush db = QBrush(QColor(200, 255, 200, 255));
            static const QBrush nb = QBrush(QColor(200, 255, 255, 255));
            painter->setPen(Qt::NoPen);
            painter->setBrush(isDir ? db : nb);
            painter->drawRect(image_rect);

            static const QPen p = QPen(QColor(100, 100, 100, 255));
            painter->setPen(p);
            painter->setBrush(Qt::NoBrush);
            painter->setFont(QFont(DEFAULT_FONT.family(), image_rect.width() / 10));
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->drawText(image_rect, Qt::AlignCenter,
                              isDir
                              ? QStringLiteral("Directory")
                              : QStringLiteral("NoImage"));
            painter->setRenderHint(QPainter::Antialiasing, false);
        }
    }

    {   // icon.
        QIcon icon;

        static QIcon blank    = QIcon(":/resources/blank.png");
        static QIcon folder   = QIcon(":/resources/folder.png");
        static QIcon folded   = QIcon(":/resources/folded.png");
        static QIcon unfolded = QIcon(":/resources/unfolded.png");

        static QIcon blankw    = QIcon(":/resources/blankw.png");
        static QIcon folderw   = QIcon(":/resources/folderw.png");
        static QIcon foldedw   = QIcon(":/resources/foldedw.png");
        static QIcon unfoldedw = QIcon(":/resources/unfoldedw.png");

        bool foldable = m_TreeBar->orientation() == Qt::Vertical;
        bool isFolded = m_Node->GetFolded();
        bool internal = false;
        if(view){
            icon = view->GetIcon();
        }
        if(icon.isNull() || icon.availableSizes().first().width() <= 2){
            icon = m_Node->GetIcon();
        }
        if(icon.isNull() || icon.availableSizes().first().width() <= 2){
            if(contrast)
                icon  = !isDir ? blankw : !foldable ? folderw : isFolded ? foldedw : unfoldedw;
            else icon = !isDir ? blank  : !foldable ? folder  : isFolded ? folded  : unfolded;
            internal = true;
        }

        QSize iconSize = m_TreeBar->ScaleByDevice(QSize(16, 16));
        QPixmap pixmap = icon.pixmap(iconSize, (view || isDir) ? QIcon::Normal : QIcon::Disabled);

        if(title_rect.isValid() && pixmap.width() > 2){
            QRect iconRect = title_rect.intersected
                (QRect(title_rect.topLeft() + QPoint(0, 2), iconSize));
            if(contrast && !internal){
                static const QBrush b = QBrush(QColor(255, 255, 255, 255));
                painter->setBrush(b);
                painter->setPen(Qt::NoPen);
                painter->drawRect(QRect(iconRect.topLeft() - QPoint(1, 1),
                                        iconRect.size() + QSize(2, 2)));
            }
            if(muted || audible) painter->setOpacity(0.66);
            painter->drawPixmap(iconRect, pixmap, QRect(QPoint(), pixmap.size()));
            if(muted || audible) painter->setOpacity(1.0);
            title_rect.setLeft(title_rect.left() + m_TreeBar->ScaleByDevice(20));
        }
    }

    {   // title.
        static const QPen bp = QPen(QColor(  0,   0,   0, 255));
        static const QPen wp = QPen(QColor(255, 255, 255, 255));
        painter->setPen(contrast ? wp : bp);
        painter->setBrush(Qt::NoBrush);
        painter->setFont(TREE_BAR_TITLE_FONT);
        if(title_rect.isValid()){
            painter->drawText(bound.intersected(title_rect),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              m_Node->ReadableTitle());
        }
    }

    if(m_IsHovered){
        static const QBrush nb  = QBrush(QColor(  0,   0,   0, 20));
        static const QBrush tb  = QBrush(QColor(  0,   0,   0, 10));
        static const QBrush fnb = QBrush(QColor(255, 255, 255, 100));
        static const QBrush ftb = QBrush(QColor(255, 255, 255, 50));
        painter->setBrush(Application::EnableTransparentBar()
                          ? (m_IsFocused ? ftb : tb)
                          : (m_IsFocused ?  nb : nb));
        painter->setPen(Qt::NoPen);
        QRectF rect = bound;
        if(!Application::EnableTransparentBar()){
            switch(m_TreeBar->orientation()){
            case Qt::Horizontal: rect.setBottom(rect.bottom() + 1); break;
            case Qt::Vertical:   rect.setRight(rect.right() + 1);   break;
            }
        }
        painter->drawRect(rect);
    }

    if(TreeBar::EnableCloseButton() && m_IsHovered){
        static QPixmap close = QPixmap(":/resources/treebar/close.png");

        if(m_ButtonState == CloseHovered || m_ButtonState == ClosePressed || contrast){
            static const QBrush h = QBrush(QColor(180, 180, 180, 255));
            static const QBrush p = QBrush(QColor(150, 150, 150, 255));
            static const QBrush c = QBrush(QColor(210, 210, 210, 255));
            painter->setBrush(m_ButtonState == CloseHovered ? h :
                              m_ButtonState == ClosePressed ? p : c);
            painter->setPen(Qt::NoPen);
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->drawRoundedRect(bound.intersected(CloseButtonRect()), 3, 3);
            painter->setRenderHint(QPainter::Antialiasing, false);
        }
        painter->drawPixmap(bound.intersected(CloseIconRect()),
                            close, QRect(QPoint(), close.size()));
    }

    if(TreeBar::EnableCloneButton() && m_IsHovered){
        static QPixmap clone = QPixmap(":/resources/treebar/clone.png");

        if(m_ButtonState == CloneHovered || m_ButtonState == ClonePressed || contrast){
            static const QBrush h = QBrush(QColor(180, 180, 180, 255));
            static const QBrush p = QBrush(QColor(150, 150, 150, 255));
            static const QBrush c = QBrush(QColor(210, 210, 210, 255));
            painter->setBrush(m_ButtonState == CloneHovered ? h :
                              m_ButtonState == ClonePressed ? p : c);
            painter->setPen(Qt::NoPen);
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->drawRoundedRect(bound.intersected(CloneButtonRect()), 3, 3);
            painter->setRenderHint(QPainter::Antialiasing, false);
        }
        painter->drawPixmap(bound.intersected(CloneIconRect()),
                            clone, QRect(QPoint(), clone.size()));
    }

    if(muted || audible){
        if(m_ButtonState == SoundHovered || m_ButtonState == SoundPressed){
            static const QBrush h = QBrush(QColor(180, 180, 180, 100));
            static const QBrush p = QBrush(QColor(150, 150, 150, 100));
            static const QBrush c = QBrush(QColor(210, 210, 210, 100));
            painter->setBrush(m_ButtonState == SoundHovered ? h :
                              m_ButtonState == SoundPressed ? p : c);
            painter->setPen(Qt::NoPen);
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->drawRoundedRect(bound.intersected(SoundButtonRect()), 3, 3);
            painter->setRenderHint(QPainter::Antialiasing, false);
        }
        if(muted){
            static QPixmap muted_ = QPixmap(":/resources/treebar/muted.png");
            painter->drawPixmap(bound.intersected(SoundIconRect()),
                                muted_, QRect(QPoint(), muted_.size()));
        } else if(audible){
            static QPixmap audible_ = QPixmap(":/resources/treebar/audible.png");
            painter->drawPixmap(bound.intersected(SoundIconRect()),
                                audible_, QRect(QPoint(), audible_.size()));
        }
    }

    if(!Application::EnableTransparentBar() && m_IsFocused){
        static const QPen p = QPen(QColor(150, 150, 150, 255));
        painter->setPen(p);
        painter->setBrush(Qt::NoBrush);
        QRectF rect = bound;
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            rect.setRight(rect.right() - 1);
            painter->drawLine(rect.bottomLeft(),
                              rect.topLeft());
            painter->drawLine(rect.topLeft(),
                              rect.topRight());
            painter->drawLine(rect.topRight(),
                              rect.bottomRight());
            break;
        case Qt::Vertical:
            rect.setBottom(rect.bottom() - 1);
            painter->drawLine(rect.topRight(),
                              rect.topLeft());
            painter->drawLine(rect.topLeft(),
                              rect.bottomLeft());
            painter->drawLine(rect.bottomLeft(),
                              rect.bottomRight());
            break;
        }
    }

    painter->restore();
}

QRectF NodeItem::boundingRect() const {
    if(isSelected() || !Layer()) return m_Rect;
    QRectF rect = m_Rect;
    switch(m_TreeBar->orientation()){
    case Qt::Horizontal:
        rect.moveLeft(rect.left() - static_cast<int>(Layer()->GetScroll()));
        return rect;
    case Qt::Vertical:
        rect.moveTop(rect.top() - static_cast<int>(Layer()->GetScroll()));
        return rect;
    }
    return m_Rect;
}

QRectF NodeItem::CloseButtonRect() const {
    if(!TreeBar::EnableCloseButton()) return QRectF();
    QRectF rect = QRectF(boundingRect().bottomRight() + QPointF(-19, -20), QSizeF(16, 16));
    return rect;
}

QRectF NodeItem::CloneButtonRect() const {
    if(!TreeBar::EnableCloneButton()) return QRectF();
    QRectF rect = QRectF(boundingRect().bottomRight() + QPointF(-19, -20), QSizeF(16, 16));
    if(TreeBar::EnableCloseButton()) rect.moveLeft(rect.left() - 18);
    return rect;
}

QRectF NodeItem::SoundButtonRect() const {
    View *view = m_Node->GetView();
    if(!view || (!view->IsAudioMuted() && !view->RecentlyAudible())) return QRectF();
    QRectF rect = QRectF(boundingRect().bottomLeft() + QPointF(4, -20), QSizeF(16, 16));

    return rect;
}

QRect NodeItem::CloseIconRect() const {
    QRect rect = QRect(boundingRect().bottomRight().toPoint() + QPoint(-16, -17), QSize(10, 10));
    return rect;
}

QRect NodeItem::CloneIconRect() const {
    QRect rect = QRect(boundingRect().bottomRight().toPoint() + QPoint(-16, -17), QSize(10, 10));
    if(TreeBar::EnableCloseButton()) rect.moveLeft(rect.left() - 18);
    return rect;
}

QRect NodeItem::SoundIconRect() const {
    QRect rect = QRect(boundingRect().bottomLeft().toPoint() + QPoint(7, -17), QSize(10, 10));
    return rect;
}

int NodeItem::GetNest() const {
    return m_Nest;
}

void NodeItem::SetNest(int nest){
    m_Nest = nest;
}

bool NodeItem::GetFocused() const {
    return m_IsFocused;
}

void NodeItem::SetFocused(bool focused){
    m_IsFocused = focused;

    if(focused){
        Layer()->SetFocusedNode(this);
        setZValue(FOCUSED_NODE_LAYER);
    } else if(zValue() != DRAGGING_NODE_LAYER){
        setZValue(NORMAL_NODE_LAYER);
    }
}

QRectF NodeItem::GetRect() const {
    return m_Rect;
}

void NodeItem::SetRect(QRectF rect){
    m_Rect = rect;
    if(Layer()) Layer()->update();
}

QPointF NodeItem::GetTargetPosition() const {
    return m_TargetPosition;
}

void NodeItem::SetTargetPosition(QPointF pos){
    m_TargetPosition = pos;
}

LayerItem *NodeItem::Layer() const {
    return static_cast<LayerItem*>(parentItem());
}

Node *NodeItem::GetNode() const {
    return m_Node;
}

QPropertyAnimation *NodeItem::GetAnimation() const {
    return m_Animation;
}

bool NodeItem::IsLocked() const {
    return m_Animation->state() == QAbstractAnimation::Running;
}

void NodeItem::SetButtonState(ButtonState state){
    if(m_ButtonState != state){
        m_ButtonState = state;
        update();
    }
}

void NodeItem::SetHoveredWithItem(bool hovered){
    if(m_Node->IsDirectory() && m_IsHovered != hovered){
        m_IsHovered = hovered;
        if(m_IsHovered){
            m_HoveredTimerId = startTimer(500);
        } else {
            killTimer(m_HoveredTimerId);
            m_HoveredTimerId = 0;
        }
        update();
    }
}

void NodeItem::UnfoldDirectory(){
    int previousHeight = m_TreeBar->GetHorizontalNodeHeight();
    NodeItem *focused = Layer()->GetFocusedNode();
    NodeItem *selected = 0;

    QList<LayerItem*> &layers = m_TreeBar->GetLayerList();
    int index = layers.indexOf(Layer());
    m_TreeBar->ClearLowerLayer(index);
    Layer()->SetNode(GetNode());
    foreach(NodeItem *item, Layer()->GetNodeItems()){
        if(item->isSelected()){
            selected = item;
            item->setSelected(false);
        }
        item->SetFocused(item == this);
    }
    Node *primary = m_Node->GetPrimary();
    if(focused && primary == focused->GetNode()) primary = 0;
    LayerItem *layer = new LayerItem(m_TreeBank, m_TreeBar, primary, m_Node);
    layers << layer;
    scene()->addItem(layer);
    NodeList list = m_Node->GetChildren();
    int i = index + 1;
    bool skipped = false;
    for(int j = 0; j < list.length(); j++){
        if(selected && selected->GetNode() == list[j]){
            skipped = true;
        } else if(skipped){
            layer->CreateNodeItem(list[j], i, j-1, 0, previousHeight);
        } else {
            layer->CreateNodeItem(list[j], i, j, 0, previousHeight);
        }
    }
    layer->Adjust();
    if(selected) Layer()->SetFocusedNode(selected);
    m_TreeBar->resize(m_TreeBar->width(),
                      (previousHeight + 3) * layers.length() + 4);
    m_TreeBar->Adjust();
    layer->SetFocusedNode(focused);
    if(selected) selected->setSelected(true);
}

void NodeItem::OnCreated(QRectF target, QRectF start){
    if(TreeBar::EnableAnimation()){
        m_Animation->setEndValue(target);
        if(start.isNull()){
            switch(m_TreeBar->orientation()){
            case Qt::Horizontal: target.setWidth(0);  break;
            case Qt::Vertical:   target.setHeight(0); break;
            }
        }
        m_Animation->setStartValue(start.isNull() ? target : start);
        m_Animation->start();
        m_Animation->setCurrentTime(16);
    } else {
        SetRect(target);
    }
}

void NodeItem::OnDeleted(QRectF target, QRectF start){
    NodeItem *item = this;
    LayerItem *layer = Layer();
    if(TreeBar::EnableAnimation()){

        if(zValue() == DELETED_NODE_LAYER) return;
        setZValue(DELETED_NODE_LAYER);

        m_Animation->setStartValue(start.isNull() ? m_Rect : start);
        if(m_Animation->state() == QAbstractAnimation::Running &&
           m_Animation->endValue().isValid()){
            target.moveTopLeft(m_Animation->endValue().toRectF().topLeft());
        }
        m_Animation->setEndValue(target);
        m_Animation->start();
        m_Animation->setCurrentTime(16);
        connect(m_Animation, &QPropertyAnimation::finished,
                this, &QObject::deleteLater);
        connect(m_Animation, &QPropertyAnimation::finished,
                [item, layer](){
                    layer->RemoveFromNodeItems(item);
                    layer->OnScrolled();
                });
    } else {
        SetRect(target);
        deleteLater();
        connect(this, &QObject::destroyed,
                [item, layer](){
                    layer->RemoveFromNodeItems(item);
                    layer->OnScrolled();
                });
    }
}

void NodeItem::OnNestChanged(){
    QRectF rect = m_Rect;

    if(TreeBar::EnableAnimation()){
        m_Animation->setStartValue(rect);
        rect.setLeft(1+m_Nest*20);
        m_Animation->setEndValue(rect);
        m_Animation->start();
        m_Animation->setCurrentTime(16);
    } else {
        rect.setLeft(1+m_Nest*20);
        SetRect(rect);
    }
}

void NodeItem::OnUngrabbed(){
    if(!TreeBar::EnableAnimation()){
        Layer()->ApplyChildrenOrder();
        return;
    }

    QList<NodeItem*> items = Layer()->GetNodeItems();
    int index = items.indexOf(this);
    QRectF target = m_Rect;
    if(index >= 1){
        NodeItem *item = items[index-1];
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            target.moveLeft((item->m_TargetPosition.isNull()
                             ? item->m_Rect.left()
                             : item->m_TargetPosition.x())
                            + m_TreeBar->GetHorizontalNodeWidth());
            break;
        case Qt::Vertical:
            target.moveTop((item->m_TargetPosition.isNull()
                            ? item->m_Rect.top()
                            : item->m_TargetPosition.y())
                           + m_TreeBar->GetVerticalNodeHeight());
            break;
        }
    } else if(index < items.length()-1){
        NodeItem *item = items[index+1];
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            target.moveLeft((item->m_TargetPosition.isNull()
                             ? item->m_Rect.left()
                             : item->m_TargetPosition.x())
                            - m_TreeBar->GetHorizontalNodeWidth());
            break;
        case Qt::Vertical:
            target.moveTop((item->m_TargetPosition.isNull()
                            ? item->m_Rect.top()
                            : item->m_TargetPosition.y())
                           - m_TreeBar->GetVerticalNodeHeight());
            break;
        }
    }

    if(m_TreeBar->orientation() == Qt::Vertical &&
       m_Animation->state() == QAbstractAnimation::Running &&
       m_Animation->endValue().isValid()){
        target.setLeft(m_Animation->endValue().toRectF().left());
    }
    m_Rect.translate(pos());
    setSelected(false);
    setPos(QPointF());
    setZValue(DRAGGING_NODE_LAYER);
    m_Animation->setStartValue(m_Rect);
    m_Animation->setEndValue(target);
    m_Animation->start();
    m_Animation->setCurrentTime(16);
    connect(m_Animation, &QPropertyAnimation::finished,
            this, &NodeItem::ApplySiblingsOrder);

    Layer()->LockWhileAnimating();
}

void NodeItem::Slide(int step){
    if(TreeBar::EnableAnimation()){
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            if(m_TargetPosition.isNull()) m_TargetPosition = m_Rect.topLeft();
            m_TargetPosition.rx() += m_TreeBar->GetHorizontalNodeWidth() * step;
            break;
        case Qt::Vertical:
            if(m_TargetPosition.isNull()) m_TargetPosition = m_Rect.topLeft();
            m_TargetPosition.ry() += m_TreeBar->GetVerticalNodeHeight() * step;
            break;
        }
        m_Animation->setStartValue(m_Rect);
        QRectF rect = QRectF(m_TargetPosition, m_Rect.size());
        if(m_Animation->state() == QAbstractAnimation::Running &&
           m_Animation->endValue().isValid()){
            rect.setSize(m_Animation->endValue().toRectF().size());
            if(rect.top() == DBL_MAX || rect.left() == DBL_MAX)
                rect.moveTopLeft(m_Animation->endValue().toRectF().topLeft());
        }
        m_Animation->setEndValue(rect);
        m_Animation->start();
        m_Animation->setCurrentTime(16);
    } else {
        QRectF rect = m_Rect;
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            rect.moveLeft(rect.left() + m_TreeBar->GetHorizontalNodeWidth() * step);
            SetRect(rect);
            break;
        case Qt::Vertical:
            rect.moveTop(rect.top() + m_TreeBar->GetVerticalNodeHeight() * step);
            SetRect(rect);
            break;
        }
    }
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value){
    if(change == ItemPositionChange && scene()){
        QPointF newPos = value.toPointF();
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            Cramp(newPos.rx(),
                  -boundingRect().left() + FRINGE_BUTTON_SIZE,
                  -boundingRect().left() - FRINGE_BUTTON_SIZE
                  + scene()->sceneRect().width() - m_TreeBar->GetHorizontalNodeWidth());
            newPos.setY(0);
            break;
        case Qt::Vertical:
            Cramp(newPos.ry(),
                  -boundingRect().top() + FRINGE_BUTTON_SIZE,
                  -boundingRect().top() - FRINGE_BUTTON_SIZE
                  + scene()->sceneRect().height() - m_TreeBar->GetVerticalNodeHeight());
            newPos.setX(0);
            break;
        }
        return newPos;
    }
    if(change == ItemSelectedChange && scene()){
        if(value.toBool()){
            setZValue(DRAGGING_NODE_LAYER);
        } else if(m_IsFocused){
            setZValue(FOCUSED_NODE_LAYER);
        } else {
            setZValue(NORMAL_NODE_LAYER);
        }
    }
    return QGraphicsObject::itemChange(change, value);
}

void NodeItem::timerEvent(QTimerEvent *ev){
    if(ev->timerId() == m_HoveredTimerId){

        killTimer(m_HoveredTimerId);
        m_HoveredTimerId = 0;

        UnfoldDirectory();
    }
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent *ev){

    if(Layer()->IsLocked() || IsLocked()){
        ev->setAccepted(false);
        return;
    }

    if(ev->button() == Qt::LeftButton){

        Layer()->SetFocusedNode(this);

        if     (CloseButtonRect().contains(ev->pos())) SetButtonState(ClosePressed);
        else if(CloneButtonRect().contains(ev->pos())) SetButtonState(ClonePressed);
        else if(SoundButtonRect().contains(ev->pos())) SetButtonState(SoundPressed);

        // m_Rect is used for boundingRect.
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            m_Rect.moveLeft(m_Rect.left() - static_cast<int>(Layer()->GetScroll()));
            break;
        case Qt::Vertical:
            m_Rect.moveTop(m_Rect.top() - static_cast<int>(Layer()->GetScroll()));
            break;
        }
        if(Application::HasCtrlModifier(ev))
            setSelected(true);
    }
    QGraphicsObject::mousePressEvent(ev);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *ev){
    bool windowCreated = m_TreeBar->TabWindowVisible();
    m_TreeBar->CloseTabWindow();

    if(ev->button() == Qt::LeftButton){

        Layer()->AutoScrollStop();
        Layer()->SetFocusedNode(0);

        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            m_Rect.moveLeft(m_Rect.left() + static_cast<int>(Layer()->GetScroll()));
            break;
        case Qt::Vertical:
            m_Rect.moveTop(m_Rect.top() + static_cast<int>(Layer()->GetScroll()));
            break;
        }

        if((ev->buttonDownScreenPos(Qt::LeftButton) - ev->screenPos()).manhattanLength() < 4){
            switch(m_ButtonState){

            case ClosePressed:
                m_TreeBank->DeleteNode(m_Node);
                break;

            case ClonePressed:
                m_TreeBank->CloneViewNode(m_Node->ToViewNode());
                break;

            case SoundPressed:
                setSelected(false);
                setPos(QPointF());
                if(View *view = m_Node->GetView()){
                    if(view->IsAudioMuted() || view->RecentlyAudible())
                        view->SetAudioMuted(!view->IsAudioMuted());
                }
                break;

            default:
                setSelected(false);
                setPos(QPointF());
                if(m_TreeBar->orientation() == Qt::Vertical && m_Node->IsDirectory()){
                    m_Node->SetFolded(!m_Node->GetFolded());
                    TreeBank::EmitFoldedChanged(NodeList() << m_Node);
                } else if(!m_TreeBank->SetCurrent(m_Node)){
                    if(m_Node->IsDirectory()){
                        UnfoldDirectory();
                    }
                }
            }
            return;
        }

        if(windowCreated){
            m_TreeBar->CollectNodes();
        } else {
            OnUngrabbed();
        }

    } else if(ev->button() == Qt::RightButton){
        if((ev->buttonDownScreenPos(Qt::RightButton) - ev->screenPos()).manhattanLength() < 4){
            QMenu *menu = NodeMenu();
            menu->exec(ev->screenPos());
            delete menu;
            return;
        }
    } else if(ev->button() == Qt::MidButton && TreeBar::WheelClickToClose()){
        if((ev->buttonDownScreenPos(Qt::MidButton) - ev->screenPos()).manhattanLength() < 4){
            m_TreeBank->DeleteNode(m_Node);
            return;
        }
    }
    // some time cause crash.
    //QGraphicsObject::mouseReleaseEvent(ev);
}

void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent *ev){

    if(m_ButtonState == ClosePressed ||
       m_ButtonState == ClonePressed ||
       m_ButtonState == SoundPressed){

        return;
    }

    QGraphicsObject::mouseMoveEvent(ev);

    if(!(ev->buttons() & Qt::LeftButton)) return;

    if(this != Layer()->GetFocusedNode())
        Layer()->SetFocusedNode(this);

    LayerItem *newLayer = 0;

    foreach(QGraphicsItem *item, scene()->items(ev->scenePos())){
        newLayer = dynamic_cast<LayerItem*>(item);
        if(newLayer) break;
    }
    if(!newLayer){
        switch(m_TreeBar->orientation()){
        case Qt::Horizontal:
            if(ev->scenePos().y() < scene()->sceneRect().top())
                newLayer = m_TreeBar->GetLayerList().first();
            else if(ev->scenePos().y() > scene()->sceneRect().bottom())
                newLayer = m_TreeBar->GetLayerList().last();
            break;
        case Qt::Vertical:
            if(ev->scenePos().x() < scene()->sceneRect().left())
                newLayer = m_TreeBar->GetLayerList().first();
            else if(ev->scenePos().x() > scene()->sceneRect().right())
                newLayer = m_TreeBar->GetLayerList().last();
            break;
        }
    }
    if(newLayer && Layer() != newLayer){
        Layer()->AutoScrollStop();
        Layer()->TransferNodeItem(this, newLayer);
    }

    if(!m_Node->IsDirectory()){
        if(scene()->sceneRect().contains(ev->scenePos())){
            m_TreeBar->HideTabWindow();
        } else if(m_TreeBar->TabWindowVisible()){
            m_TreeBar->MoveTabWindow(ev->scenePos().toPoint());
            return;
        } else {
            m_TreeBar->ShowTabWindow(ev->scenePos().toPoint(), m_Node);
            return;
        }
    }

    switch(m_TreeBar->orientation()){
    case Qt::Horizontal:
        if(ev->scenePos().x() < scene()->sceneRect().left() + FRINGE_BUTTON_SIZE)
            Layer()->AutoScrollUp();
        else if(ev->scenePos().x() > scene()->sceneRect().right() - FRINGE_BUTTON_SIZE)
            Layer()->AutoScrollDown();
        else
            Layer()->AutoScrollStop();
        break;
    case Qt::Vertical:
        if(ev->scenePos().y() < scene()->sceneRect().top() + FRINGE_BUTTON_SIZE)
            Layer()->AutoScrollUp();
        else if(ev->scenePos().y() > scene()->sceneRect().bottom() - FRINGE_BUTTON_SIZE)
            Layer()->AutoScrollDown();
        else
            Layer()->AutoScrollStop();
        break;
    }

    if(m_TreeBar->orientation() == Qt::Horizontal){
        foreach(NodeItem *item, Layer()->GetNodeItems()){
            if(item != this){
                QRectF r1 = item->boundingRect().translated(item->pos());
                QRectF r2 = r1.intersected(boundingRect().translated(pos()));
                item->SetHoveredWithItem(r2.width() > r1.width() / 4.0);
            }
        }
    }
    Layer()->CorrectOrder();
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *ev){
    QGraphicsObject::mouseDoubleClickEvent(ev);
}

void NodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent *ev){
    m_IsHovered = true;
    if     (CloseButtonRect().contains(ev->pos())) SetButtonState(CloseHovered);
    else if(CloneButtonRect().contains(ev->pos())) SetButtonState(CloneHovered);
    else if(SoundButtonRect().contains(ev->pos())) SetButtonState(SoundHovered);
    else SetButtonState(NotHovered);
    QGraphicsObject::hoverEnterEvent(ev);
}

void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *ev){
    m_IsHovered = false;
    SetButtonState(NotHovered);
    QGraphicsObject::hoverLeaveEvent(ev);
}

void NodeItem::hoverMoveEvent(QGraphicsSceneHoverEvent *ev){
    m_IsHovered = true;
    if     (CloseButtonRect().contains(ev->pos())) SetButtonState(CloseHovered);
    else if(CloneButtonRect().contains(ev->pos())) SetButtonState(CloneHovered);
    else if(SoundButtonRect().contains(ev->pos())) SetButtonState(SoundHovered);
    else SetButtonState(NotHovered);
    QGraphicsObject::hoverMoveEvent(ev);
}

void NodeItem::wheelEvent(QGraphicsSceneWheelEvent *ev){
    // call LayerItem's wheel event.
    QGraphicsObject::wheelEvent(ev);
}

QPointF NodeItem::ScheduledPosition(){
    if(m_TargetPosition.isNull()) return boundingRect().topLeft() + pos();
    switch(m_TreeBar->orientation()){
    case Qt::Horizontal: return m_TargetPosition - QPointF(Layer()->GetScroll(), 0);
    case Qt::Vertical:   return m_TargetPosition - QPointF(0, Layer()->GetScroll());
    }
    return m_TargetPosition;
}

QMenu *NodeItem::NodeMenu(){
    QMenu *menu = new QMenu(m_TreeBar);

    TreeBank *tb = m_TreeBank;
    ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0;
    if(!vn) return menu;

    QAction *newViewNode = new QAction(menu);
    newViewNode->setText(QObject::tr("NewViewNode"));
    newViewNode->connect(newViewNode, &QAction::triggered, this, &NodeItem::NewViewNode);
    menu->addAction(newViewNode);

    QAction *cloneViewNode = new QAction(menu);
    cloneViewNode->setText(QObject::tr("CloneViewNode"));
    cloneViewNode->connect(cloneViewNode, &QAction::triggered, this, &NodeItem::CloneViewNode);
    menu->addAction(cloneViewNode);

    menu->addSeparator();

    if(vn->IsDirectory()){
        QAction *rename = new QAction(menu);
        rename->setText(QObject::tr("RenameViewNode"));
        rename->connect(rename, &QAction::triggered, this, &NodeItem::RenameViewNode);
        menu->addAction(rename);

        QMenu *directoryMenu = new QMenu(tr("DirectorySettings"), menu);

        QWidgetAction *settings = new QWidgetAction(directoryMenu);
        QWidget *widget = new QWidget(directoryMenu);
        QGridLayout *layout = new QGridLayout();
        static const QList<QStringList> list =
            QList<QStringList>()
            << (QStringList() << tr("Use name as profile ID.") << QStringLiteral(";[iI][dD](?:entif(?:y|ier|ication))?") << QStringLiteral(";ID"))
            << (QStringList() << tr("Private mode.") << QStringLiteral(";(?:[pP]rivate|[oO]ff[tT]he[rR]ecord)") << QStringLiteral(";Private"))
            << (QStringList() << tr("Disable auto load.") << QStringLiteral(";!?[nN](?:o)?(?:[aA](?:uto)?)?[lL](?:oad)?") << QStringLiteral(";NoAutoLoad") << QStringLiteral(";!NoAutoLoad"))
            << (QStringList() << tr("Enable drag gestuer.") << QStringLiteral(";!?[dD](?:rag)?[gG](?:esture)?") << QStringLiteral(";DragGesture") << QStringLiteral(";!DragGesture"))
            << (QStringList() << tr("Auto load images.") << QStringLiteral(";!?[iI]mage") << QStringLiteral(";Image") << QStringLiteral(";!Image"))
            << (QStringList() << tr("Enable javascript.") << QStringLiteral(";!?[jJ](?:ava)?[sS](?:cript)?") << QStringLiteral(";Javascript") << QStringLiteral(";!Javascript"))
            << (QStringList() << tr("Enable plugins.") << QStringLiteral(";!?[pP]lugins?") << QStringLiteral(";Plugins") << QStringLiteral(";!Plugins"));

        for(int i = 0; i < list.length(); i++){
            QCheckBox *checkBox = new QCheckBox();
            checkBox->setChecked(vn->GetTitle().contains(QRegularExpression(list[i][1])));
            QLabel *label = new QLabel(list[i][0]);
            connect(checkBox, &QCheckBox::stateChanged,
                    [vn, i](int state){
                        QString before = vn->GetTitle();
                        QString after = before;
                        switch(state){
                        case Qt::Checked:
                            if(list[i].length() > 3)
                                after.replace(QRegularExpression(list[i][1]), QString());
                            after = after + list[i][2];
                            break;
                        case Qt::Unchecked:
                            after.replace(QRegularExpression(list[i][1]), QString());
                            if(list[i].length() > 3)
                                after = after + list[i][3];
                            break;
                        }
                        vn->SetTitle(after);
                        TreeBank::ReconfigureDirectory(vn, before, after);
                    });
            layout->addWidget(checkBox, i, 0);
            layout->addWidget(label, i, 1);
        }

        widget->setLayout(layout);
        settings->setDefaultWidget(widget);
        directoryMenu->addAction(settings);

        menu->addMenu(directoryMenu);

    } else {
        QAction *reload = new QAction(menu);
        reload->setText(QObject::tr("ReloadViewNode"));
        reload->connect(reload, &QAction::triggered, this, &NodeItem::ReloadViewNode);
        menu->addAction(reload);

        if(!tb->IsCurrent(vn)){
            QAction *open = new QAction(menu);
            open->setText(QObject::tr("OpenViewNode"));
            open->connect(open, &QAction::triggered, this, &NodeItem::OpenViewNode);
            menu->addAction(open);
            QAction *openOnNewWindow = new QAction(menu);
            openOnNewWindow->setText(QObject::tr("OpenViewNodeOnNewWindow"));
            openOnNewWindow->connect(openOnNewWindow, &QAction::triggered,
                                     this, &NodeItem::OpenViewNodeOnNewWindow);
            menu->addAction(openOnNewWindow);
        }

        menu->addSeparator();

        QMenu *m = new QMenu(tr("OpenViewNodeWithOtherBrowser"));
        if(!Application::BrowserPath_IE().isEmpty()){
            QAction *a = new QAction(m);
            a->setText(tr("OpenViewNodeWithIE"));
            a->setIcon(Application::BrowserIcon_IE());
            a->connect(a, &QAction::triggered, this, &NodeItem::OpenViewNodeWithIE);
            m->addAction(a);
        }
        if(!Application::BrowserPath_Edge().isEmpty()){
            QAction *a = new QAction(m);
            a->setText(tr("OpenViewNodeWithEdge"));
            a->setIcon(Application::BrowserIcon_Edge());
            a->connect(a, &QAction::triggered, this, &NodeItem::OpenViewNodeWithEdge);
            m->addAction(a);
        }
        if(!Application::BrowserPath_FF().isEmpty()){
            QAction *a = new QAction(m);
            a->setText(tr("OpenViewNodeWithFF"));
            a->setIcon(Application::BrowserIcon_FF());
            a->connect(a, &QAction::triggered, this, &NodeItem::OpenViewNodeWithFF);
            m->addAction(a);
        }
        if(!Application::BrowserPath_Opera().isEmpty()){
            QAction *a = new QAction(m);
            a->setText(tr("OpenViewNodeWithOpera"));
            a->setIcon(Application::BrowserIcon_Opera());
            a->connect(a, &QAction::triggered, this, &NodeItem::OpenViewNodeWithOpera);
            m->addAction(a);
        }
        if(!Application::BrowserPath_OPR().isEmpty()){
            QAction *a = new QAction(m);
            a->setText(tr("OpenViewNodeWithOPR"));
            a->setIcon(Application::BrowserIcon_OPR());
            a->connect(a, &QAction::triggered, this, &NodeItem::OpenViewNodeWithOPR);
            m->addAction(a);
        }
        if(!Application::BrowserPath_Safari().isEmpty()){
            QAction *a = new QAction(m);
            a->setText(tr("OpenViewNodeWithSafari"));
            a->setIcon(Application::BrowserIcon_Safari());
            a->connect(a, &QAction::triggered, this, &NodeItem::OpenViewNodeWithSafari);
            m->addAction(a);
        }
        if(!Application::BrowserPath_Chrome().isEmpty()){
            QAction *a = new QAction(m);
            a->setText(tr("OpenViewNodeWithChrome"));
            a->setIcon(Application::BrowserIcon_Chrome());
            a->connect(a, &QAction::triggered, this, &NodeItem::OpenViewNodeWithChrome);
            m->addAction(a);
        }
        if(!Application::BrowserPath_Sleipnir().isEmpty()){
            QAction *a = new QAction(m);
            a->setText(tr("OpenViewNodeWithSleipnir"));
            a->setIcon(Application::BrowserIcon_Sleipnir());
            a->connect(a, &QAction::triggered, this, &NodeItem::OpenViewNodeWithSleipnir);
            m->addAction(a);
        }
        if(!Application::BrowserPath_Vivaldi().isEmpty()){
            QAction *a = new QAction(m);
            a->setText(tr("OpenViewNodeWithVivaldi"));
            a->setIcon(Application::BrowserIcon_Vivaldi());
            a->connect(a, &QAction::triggered, this, &NodeItem::OpenViewNodeWithVivaldi);
            m->addAction(a);
        }
        if(!Application::BrowserPath_Custom().isEmpty()){
            QAction *a = new QAction(m);
            a->setText(tr("OpenViewNodeWith%1").arg(Application::BrowserPath_Custom().split("/").last().replace(".exe", "")));
            a->setIcon(Application::BrowserIcon_Custom());
            a->connect(a, &QAction::triggered, this, &NodeItem::OpenViewNodeWithCustom);
            m->addAction(a);
        }
        menu->addMenu(m);
    }

    menu->addSeparator();

    QAction *deleteViewNode = new QAction(menu);
    deleteViewNode->setText(QObject::tr("DeleteViewNode"));
    deleteViewNode->connect(deleteViewNode, &QAction::triggered, this, &NodeItem::DeleteViewNode);
    menu->addAction(deleteViewNode);
    QAction *deleteRightNode = new QAction(menu);
    deleteRightNode->setText(m_TreeBar->orientation() == Qt::Horizontal
                             ? QObject::tr("DeleteRightViewNode")
                             : QObject::tr("DeleteLowerViewNode"));
    deleteRightNode->connect(deleteRightNode, &QAction::triggered,
                             this, &NodeItem::DeleteRightViewNode);
    menu->addAction(deleteRightNode);
    QAction *deleteLeftNode = new QAction(menu);
    deleteLeftNode->setText(m_TreeBar->orientation() == Qt::Horizontal
                            ? QObject::tr("DeleteLeftViewNode")
                            : QObject::tr("DeleteUpperViewNode"));
    deleteLeftNode->connect(deleteLeftNode, &QAction::triggered,
                            this, &NodeItem::DeleteLeftViewNode);
    menu->addAction(deleteLeftNode);
    QAction *deleteOtherNode = new QAction(menu);
    deleteOtherNode->setText(QObject::tr("DeleteOtherViewNode"));
    deleteOtherNode->connect(deleteOtherNode, &QAction::triggered,
                             this, &NodeItem::DeleteOtherViewNode);
    menu->addAction(deleteOtherNode);

    menu->addSeparator();

    QAction *makeDirectory = new QAction(menu);
    makeDirectory->setText(QObject::tr("MakeDirectory"));
    makeDirectory->connect(makeDirectory, &QAction::triggered, this, &NodeItem::MakeDirectory);
    menu->addAction(makeDirectory);
    QAction *makeDirectoryWithSelected = new QAction(menu);
    makeDirectoryWithSelected->setText(QObject::tr("MakeDirectoryWithSelectedNode"));
    makeDirectoryWithSelected->connect
        (makeDirectoryWithSelected, &QAction::triggered,
         this, &NodeItem::MakeDirectoryWithSelectedNode);
    menu->addAction(makeDirectoryWithSelected);
    QAction *makeDirectoryWithSameDomain = new QAction(menu);
    makeDirectoryWithSameDomain->setText(QObject::tr("MakeDirectoryWithSameDomainNode"));
    makeDirectoryWithSameDomain->connect
        (makeDirectoryWithSameDomain, &QAction::triggered,
         this, &NodeItem::MakeDirectoryWithSameDomainNode);
    menu->addAction(makeDirectoryWithSameDomain);

    return menu;
}

void NodeItem::NewViewNode(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        m_TreeBank->NewViewNode(vn);
}

void NodeItem::CloneViewNode(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        m_TreeBank->CloneViewNode(vn);
}

void NodeItem::RenameViewNode(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        m_TreeBank->RenameNode(vn);
}

void NodeItem::ReloadViewNode(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        if(View *view = vn->GetView())
            view->TriggerAction(Page::_Reload);
}

void NodeItem::OpenViewNode(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        m_TreeBank->SetCurrent(vn);
}

void NodeItem::OpenViewNodeOnNewWindow(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        m_TreeBank->NewWindow()->GetTreeBank()->SetCurrent(vn);
}

void NodeItem::DeleteViewNode(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        m_TreeBank->DeleteNode(vn);
}

void NodeItem::DeleteRightViewNode(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0){
        NodeList list;
        NodeList siblings = vn->GetSiblings();
        int index = siblings.indexOf(vn);
        for(int i = index + 1; i < siblings.length(); i++){
            list << siblings[i];
        }
        m_TreeBank->DeleteNode(list);
    }
}

void NodeItem::DeleteLeftViewNode(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0){
        NodeList list;
        NodeList siblings = vn->GetSiblings();
        int index = siblings.indexOf(vn);
        for(int i = 0; i < index; i++){
            list << siblings[i];
        }
        m_TreeBank->DeleteNode(list);
    }
}

void NodeItem::DeleteOtherViewNode(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0){
        NodeList siblings = vn->GetSiblings();
        siblings.removeOne(vn);
        m_TreeBank->DeleteNode(siblings);
    }
}

void NodeItem::MakeDirectory(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        m_TreeBank->MakeSiblingDirectory(vn);
}

void NodeItem::MakeDirectoryWithSelectedNode(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0){
        Node *parent = m_TreeBank->MakeSiblingDirectory(vn->ToViewNode());
        m_TreeBank->SetChildrenOrder(parent, NodeList() << vn);
    }
}

void NodeItem::MakeDirectoryWithSameDomainNode(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0){
        ViewNode *parent = vn->GetParent()->ToViewNode();
        QMap<QString, NodeList> groups;
        foreach(Node *nd, vn->GetSiblings()){
            if(!nd->IsDirectory()) groups[nd->GetUrl().host()] << nd;
        }
        if(groups.count() <= 1) return;
        foreach(QString domain, groups.keys()){
            Node *directory = parent->MakeChild();
            directory->SetTitle(domain);
            m_TreeBank->SetChildrenOrder(directory, groups[domain]);
        }
    }
}

void NodeItem::OpenViewNodeWithIE(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        Application::OpenUrlWith_IE(vn->GetUrl());
}

void NodeItem::OpenViewNodeWithEdge(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        Application::OpenUrlWith_Edge(vn->GetUrl());
}

void NodeItem::OpenViewNodeWithFF(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        Application::OpenUrlWith_FF(vn->GetUrl());
}

void NodeItem::OpenViewNodeWithOpera(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        Application::OpenUrlWith_Opera(vn->GetUrl());
}

void NodeItem::OpenViewNodeWithOPR(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        Application::OpenUrlWith_OPR(vn->GetUrl());
}

void NodeItem::OpenViewNodeWithSafari(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        Application::OpenUrlWith_Safari(vn->GetUrl());
}

void NodeItem::OpenViewNodeWithChrome(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        Application::OpenUrlWith_Chrome(vn->GetUrl());
}

void NodeItem::OpenViewNodeWithSleipnir(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        Application::OpenUrlWith_Sleipnir(vn->GetUrl());
}

void NodeItem::OpenViewNodeWithVivaldi(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        Application::OpenUrlWith_Vivaldi(vn->GetUrl());
}

void NodeItem::OpenViewNodeWithCustom(){
    if(ViewNode *vn = GetNode() ? GetNode()->ToViewNode() : 0)
        Application::OpenUrlWith_Custom(vn->GetUrl());
}

void NodeItem::ResetTargetPosition(){
    m_TargetPosition = QPointF();
}

void NodeItem::ApplySiblingsOrder(){
    disconnect(m_Animation, &QPropertyAnimation::finished,
               this, &NodeItem::ApplySiblingsOrder);
    setSelected(true);
    Layer()->ApplyChildrenOrder();
}
