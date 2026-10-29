#include "WindowAppearance.h"

QColor WindowAppearance::surface(bool transparent, int opacity) {
    QColor color(transparent ? "#182130" : "#f3f6fb");
    color.setAlpha(transparent ? qRound(qBound(40, opacity, 95) * 255.0 / 100) : 255);
    return color;
}
QPalette WindowAppearance::palette(bool transparent) {
    QPalette result(QColor(Qt::white), QColor("#f3f6fb"));
    const QColor text(transparent ? "#ecf2ff" : "#243047");
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText})
        result.setColor(role, text);
    result.setColor(QPalette::Window, transparent ? QColor(24, 33, 48, 0) : QColor("#f3f6fb"));
    result.setColor(QPalette::Base, transparent ? QColor(24, 33, 48, 0) : QColor(Qt::white));
    result.setColor(QPalette::AlternateBase, QColor(transparent ? "#243349" : "#eef4ff"));
    result.setColor(QPalette::ToolTipBase, QColor(transparent ? "#182130" : "#ffffff"));
    result.setColor(QPalette::Highlight, QColor(transparent ? "#345782" : "#dbeafe"));
    result.setColor(QPalette::HighlightedText, QColor(transparent ? "#ffffff" : "#17396d"));
    result.setColor(QPalette::Accent, QColor("#2563eb"));
    result.setColor(QPalette::Link, QColor(transparent ? "#93c5fd" : "#2563eb"));
    result.setColor(QPalette::LinkVisited, QColor(transparent ? "#d8b4fe" : "#6d49a8"));
    result.setColor(QPalette::PlaceholderText, QColor(transparent ? "#a4b1c4" : "#8793a7"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        result.setColor(QPalette::Disabled, role, QColor(transparent ? "#a4b1c4" : "#a4adba"));
    return result;
}
QString WindowAppearance::styleSheet(bool transparent) {
    const QString text = transparent ? "#ecf2ff" : "#243047";
    const QString border = transparent ? "rgba(190,210,240,75)" : "#d7dfeb";
    const QString background = transparent ? "transparent" : "#f3f6fb";
    const QString panel = transparent ? "rgba(255,255,255,16)" : "white";
    const QString input = transparent ? "transparent" : "white";
    const QString button = transparent ? "rgba(255,255,255,22)" : "white";
    const QString hover = transparent ? "rgba(135,180,240,60)" : "#eef4ff";
    const QString dialog = transparent ? "#182130" : "#f3f6fb";
    return QStringLiteral(
               "QMainWindow{background:transparent} QWidget{font-family:'Microsoft YaHei "
               "UI';font-size:13px;color:%1} "
               "QScrollArea,QScrollArea > QWidget,QScrollArea > QWidget > QWidget{background:%3} "
               "QWidget#mainBody,QWidget#mainViewport,QScrollArea#mainScrollArea{background:%2} "
               "QDialog{background:%3} QGroupBox{background:%4;border:1px solid %5;border-radius:8px;"
               "margin-top:14px;padding-top:12px} "
               "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 5px;font-weight:600} "
               "QPushButton{background:%6;border:1px solid %5;border-radius:5px;padding:7px 12px} "
               "QPushButton:hover{background:%7} QPushButton:disabled{color:#a4adba;background:%4} "
               "QPushButton:checked,QPushButton#startButton{background:#2563eb;color:white;border:0} "
               "QLineEdit,QPlainTextEdit,QComboBox,QListWidget,QTextBrowser{background:%8;border:1px solid "
               "%5;"
               "border-radius:5px;padding:5px;selection-background-color:#345782;selection-color:white} "
               "QComboBox QAbstractItemView{background:%3;color:%1;selection-background-color:#345782} "
               "QListWidget::item{padding:9px;border-bottom:1px solid %5} "
               "QProgressBar{border:0;background:%4;border-radius:3px;height:7px} "
               "QProgressBar::chunk{background:#3b82f6;border-radius:3px} QTabWidget::pane{border:0} "
               "QTabBar::tab{background:%4;padding:5px 10px} QTabBar::tab:selected{background:%6} "
               "QLabel#caption{font-size:23px;font-weight:700;color:%9} "
               "QStatusBar,QWidget#windowTitleBar{background:transparent} QStatusBar::item{border:0} "
               "QPushButton[windowControl=true]{background:transparent;border:0;padding:0;border-radius:4px;"
               "font-size:17px} QPushButton[windowControl=true]:hover{background:%7} "
               "QPushButton#windowClose:hover{background:#dc2626;color:white} "
               "QSlider::groove:horizontal{height:4px;background:%5;border-radius:2px} "
               "QSlider::handle:horizontal{width:12px;margin:-4px 0;background:#60a5fa;border-radius:6px} "
               "QLabel#captureStatus[privacyError=true]{color:%10}")
        .arg(text, background, dialog, panel, border, button, hover, input,
             transparent ? "#ffffff" : "#152b4f", transparent ? "#ffc17b" : "#b45309");
}
