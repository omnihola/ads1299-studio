#pragma once

#include <QWidget>
#include <QColor>

namespace studio {

class LedIndicator : public QWidget {
    Q_OBJECT
public:
    enum class Status { Off, Ok, Warn, Error };

    explicit LedIndicator(QWidget* parent = nullptr);

    void setStatus(Status s);
    Status status() const;
    QColor colorFor(Status s) const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Status status_ = Status::Off;
};

} // namespace studio
