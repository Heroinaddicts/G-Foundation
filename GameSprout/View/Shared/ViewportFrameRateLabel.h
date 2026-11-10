#pragma once

#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>

// A viewport reports only successful swapchain submissions. The readout
// expires once when they stop, independently of viewport redraw scheduling.
class ViewportFrameRateLabel final : public QLabel {
public:
    explicit ViewportFrameRateLabel(QWidget* parent=nullptr) : QLabel(parent) {
        _Expiry.setSingleShot(true);
        _Expiry.setInterval(2000);
        connect(&_Expiry,&QTimer::timeout,this,&ViewportFrameRateLabel::Reset);
    }

    void FrameSubmitted() {
        _Expiry.start();
        if(!_Sample.isValid()) {
            _Sample.start();
            return;
        }
        ++_Intervals;
        const auto elapsed=_Sample.nsecsElapsed();
        if(elapsed<500'000'000) return;
        const double fps=_Intervals*1'000'000'000.0/elapsed;
        const auto value=QStringLiteral("%1 FPS").arg(fps,0,'f',1);
        if(text()!=value) setText(value);
        _Sample.start();
        _Intervals=0;
    }

    void Reset() {
        _Expiry.stop();
        _Sample.invalidate();
        _Intervals=0;
        const auto idle=QStringLiteral("— FPS");
        if(text()!=idle) setText(idle);
    }

protected:
    void hideEvent(QHideEvent* event) override {
        Reset();
        QLabel::hideEvent(event);
    }

private:
    QElapsedTimer _Sample;
    QTimer _Expiry;
    unsigned _Intervals=0;
};
