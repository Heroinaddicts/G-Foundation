#pragma once
#include <QComboBox>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
class QStandardItemModel;

// Designer-promoted ComboBox: the popup is a checkable value model, not an array of widgets.
class LayerMaskCombo final : public QComboBox {
public:
    explicit LayerMaskCombo(QWidget* parent=nullptr);
    void SetLayers(const std::array<std::string,32>&);
    void SetMask(std::uint32_t);
    std::uint32_t GetMask() const {return _Mask;}
    std::function<void(std::uint32_t)> MaskChanged;
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void paintEvent(QPaintEvent*) override;
private:
    void Toggle(int);
    void RefreshChecks();
    QString Summary() const;
    QStandardItemModel* _Items=nullptr;
    std::array<std::string,32> _Layers;
    std::uint32_t _Mask=0xFFFFFFFFu;
};
