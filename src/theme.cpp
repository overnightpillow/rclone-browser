#include "theme.h"

namespace {

// A hairline that reads as a separator in both schemes: the window text colour
// at low alpha, so it darkens on light backgrounds and lightens on dark ones
// without either being hardcoded.
QString hairline(const QPalette &palette) {
  QColor line = palette.color(QPalette::WindowText);
  line.setAlpha(38);
  return QString("rgba(%1,%2,%3,%4)")
      .arg(line.red())
      .arg(line.green())
      .arg(line.blue())
      .arg(line.alpha());
}

// Row hover. Deliberately fainter than selection, which the style draws with
// the system accent colour.
QString hoverBackground(const QPalette &palette) {
  QColor hover = palette.color(QPalette::WindowText);
  hover.setAlpha(20);
  return QString("rgba(%1,%2,%3,%4)")
      .arg(hover.red())
      .arg(hover.green())
      .arg(hover.blue())
      .arg(hover.alpha());
}

// Reapplies the sheet when the system flips between light and dark. Without
// this the app keeps whichever palette it started with until relaunch.
class ThemeWatcher : public QObject {
public:
  explicit ThemeWatcher(QApplication *app) : QObject(app), mApp(app) {}

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::ThemeChange) {
      mApp->setStyleSheet(ThemeStyleSheet(mApp->palette()));
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QApplication *mApp;
};

} // namespace

QColor SecondaryTextColor(const QPalette &palette) {
  // Derived from Text rather than read from PlaceholderText: that role is not
  // guaranteed to be set, and where it is unset it is identical in both
  // schemes, which would leave muted columns unreadable in one of them.
  // Alpha against the actual text colour always adapts.
  QColor colour = palette.color(QPalette::Text);
  colour.setAlpha(160);
  return colour;
}

QFont SecondaryFont(const QFont &base) {
  QFont font = base;
  if (font.pointSizeF() > 0) {
    font.setPointSizeF(font.pointSizeF() * 0.92);
  }
  return font;
}

namespace {

QString rgba(const QColor &colour) {
  return QString("rgba(%1,%2,%3,%4)")
      .arg(colour.red())
      .arg(colour.green())
      .arg(colour.blue())
      .arg(colour.alpha());
}

} // namespace

QString ThemeStyleSheet(const QPalette &palette) {
  const QString line = hairline(palette);
  const QString hover = hoverBackground(palette);

  const QString selectionBackground =
      rgba(palette.color(QPalette::Active, QPalette::Highlight));
  const QString selectionText =
      rgba(palette.color(QPalette::Active, QPalette::HighlightedText));

  // When the window is not focused macOS greys the selection out; the text
  // stays the normal colour rather than the highlighted one.
  QColor inactive = palette.color(QPalette::Active, QPalette::Highlight);
  inactive.setAlpha(90);
  const QString inactiveBackground = rgba(inactive);
  const QString normalText = rgba(palette.color(QPalette::Text));

  return QString(R"(
/* Views fill their tab; a frame around them is redundant chrome. */
QTreeView, QListWidget {
    border: none;
    outline: none;
}

/* Rows at the platform default are cramped. Height and padding are the
   cheapest thing that separates a current-looking list from a dated one. */
QTreeView::item, QListWidget::item {
    height: 26px;
    padding-left: 4px;
    padding-right: 4px;
    border: none;
}

QTreeView::item:hover, QListWidget::item:hover {
    background: %2;
}

/* A disabled row is how a non-selectable one is expressed -- the section
   headings in the remotes list are disabled items. The delegate paints those
   from the palette's Disabled group and ignores whatever foreground the item
   itself carries, and on a dark palette that group is near-black on near-black:
   the headings were there, and could not be read. Stated here so it follows the
   colour scheme rather than being fixed at the moment the row was built. */
QTreeView::item:disabled, QListWidget::item:disabled {
    color: %8;
}

/* Styling ::item at all moves Qt onto the style sheet drawing path, which
   stops painting the selection background while the text still switches to
   HighlightedText -- white on white, invisible. Both halves have to be stated
   explicitly once either is. */
QTreeView::item:selected, QListWidget::item:selected {
    background: %3;
    color: %4;
}

/* Unfocused window: muted selection, ordinary text colour. */
QTreeView::item:selected:!active, QListWidget::item:selected:!active {
    background: %5;
    color: %6;
}

/* The tab bar sat in a tall band with a heavy frame beneath it. Document mode
   plus no pane border lets it sit flush against the content. */
QTabWidget::pane {
    border: none;
    border-top: 1px solid %1;
    margin: 0px;
    padding: 0px;
}

QTabBar {
    qproperty-drawBase: 0;
    background: transparent;
}

/* Flat, no separator between neighbours. Relying on the selected tab merely
   sitting on the content background was too subtle to read at a glance, so
   the active tab is marked three ways: full-strength text against muted
   neighbours, the content background, and an accent underline. */
QTabBar::tab {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 7px 14px;
    margin: 0px;
    color: %8;
}

QTabBar::tab:hover:!selected {
    background: %2;
    color: %6;
}

QTabBar::tab:selected {
    background: %7;
    color: %6;
    font-weight: 600;
    border-bottom: 2px solid %3;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}

QTabBar::close-button {
    subcontrol-position: right;
    margin-left: 4px;
}

/* The header was a heavy grey band with a hard separator between every
   section. Flat, with a single hairline underneath. */
QHeaderView {
    background: transparent;
    border: none;
}

QHeaderView::section {
    background: transparent;
    border: none;
    border-bottom: 1px solid %1;
    padding: 5px 6px;
    margin: 0px;
}

QHeaderView::section:hover {
    background: %2;
}

/* Tool bars rendered as a gradient strip, banding the top of every tab. */
QToolBar {
    background: transparent;
    border: none;
    border-bottom: 1px solid %1;
    padding: 3px 4px;
    spacing: 2px;
}

QToolBar::separator {
    background: %1;
    width: 1px;
    margin: 4px 6px;
}

QToolButton {
    border: none;
    border-radius: 5px;
    padding: 4px 7px;
}

QToolButton:hover {
    background: %2;
}

QToolButton:pressed, QToolButton:checked {
    background: %1;
}
)")
      .arg(line, hover, selectionBackground, selectionText, inactiveBackground,
           normalText, rgba(palette.color(QPalette::Base)),
           rgba(SecondaryTextColor(palette)));
}

void InstallTheme(QApplication *app) {
  Q_ASSERT(app);
  app->setStyleSheet(ThemeStyleSheet(app->palette()));
  app->installEventFilter(new ThemeWatcher(app));
}
