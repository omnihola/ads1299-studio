#include "LedIndicator.h"
#include "Theme.h"

#include <QPainter>
#include <QPaintEvent>

namespace studio {

LedIndicator::LedIndicator(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
}

void LedIndicator::setStatus(Status s)
{
    status_ = s;
    update();
}

LedIndicator::Status LedIndicator::status() const
{
    return status_;
}

QColor LedIndicator::colorFor(Status s) const
{
    switch (s) {
        case Status::Ok:    return QColor(theme::kOk);
        case Status::Warn:  return QColor(theme::kWarn);
        case Status::Error: return QColor(theme::kError);
        case Status::Off:
        default:            return QColor(theme::kLedOff);
    }
}

QSize LedIndicator::sizeHint() const
{
    return QSize(14, 14);
}

void LedIndicator::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor color = colorFor(status_);
    const QRect r = rect();
    const int inset = 2;

    // Outer translucent glow ring
    QColor halo = color;
    halo.setAlpha(60);
    painter.setBrush(halo);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(r.adjusted(0, 0, -1, -1));

    // Inner filled circle
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(r.adjusted(inset, inset, -inset, -inset));
}

} // namespace studio
