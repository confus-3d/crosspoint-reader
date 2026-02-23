#include "RecentBooksActivity.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace {
constexpr uint32_t TXT_INDEX_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t TXT_INDEX_VERSION_MIN = 1;
constexpr uint8_t TXT_INDEX_VERSION_MAX = 2;
constexpr char READING_TIME_FILE_NAME[] = "/reading_time.bin";

uint32_t readSecondsFromCachePath(const std::string& cachePath) {
  uint32_t seconds = 0;
  FsFile f;
  if (!Storage.openFileForRead("RBA", cachePath + READING_TIME_FILE_NAME, f)) {
    return 0;
  }

  uint8_t data[4];
  if (f.read(data, sizeof(data)) == 4) {
    seconds = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
  }
  f.close();
  return seconds;
}

uint32_t loadReadSecondsForPath(const std::string& path) {
  if (StringUtils::checkFileExtension(path, ".epub")) {
    Epub epub(path, "/.crosspoint");
    return readSecondsFromCachePath(epub.getCachePath());
  }
  if (StringUtils::checkFileExtension(path, ".xtch") || StringUtils::checkFileExtension(path, ".xtc")) {
    Xtc xtc(path, "/.crosspoint");
    return readSecondsFromCachePath(xtc.getCachePath());
  }
  if (StringUtils::checkFileExtension(path, ".txt") || StringUtils::checkFileExtension(path, ".md")) {
    Txt txt(path, "/.crosspoint");
    return readSecondsFromCachePath(txt.getCachePath());
  }
  return 0;
}

int loadEpubProgressPercent(const std::string& path) {
  Epub epub(path, "/.crosspoint");
  if (!epub.load(true, true)) {
    return -1;
  }

  FsFile f;
  if (!Storage.openFileForRead("RBA", epub.getCachePath() + "/progress.bin", f)) {
    return -1;
  }

  uint8_t data[6];
  const int dataSize = f.read(data, sizeof(data));
  f.close();
  if (dataSize < 4) {
    return -1;
  }

  const int spineIndex = data[0] + (data[1] << 8);
  const int currentPage = data[2] + (data[3] << 8);
  int chapterPageCount = 0;
  if (dataSize >= 6) {
    chapterPageCount = data[4] + (data[5] << 8);
  }

  float sectionProgress = 0.0f;
  if (chapterPageCount > 0) {
    sectionProgress = static_cast<float>(currentPage) / static_cast<float>(chapterPageCount);
  }

  const float progress = epub.calculateProgress(spineIndex, sectionProgress);
  int percent = static_cast<int>(progress * 100.0f + 0.5f);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  return percent;
}

int loadXtcProgressPercent(const std::string& path) {
  Xtc xtc(path, "/.crosspoint");
  if (!xtc.load()) {
    return -1;
  }

  const uint32_t totalPages = xtc.getPageCount();
  if (totalPages == 0) {
    return -1;
  }

  uint32_t currentPage = 0;
  FsFile f;
  if (Storage.openFileForRead("RBA", xtc.getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    }
    f.close();
  }
  if (currentPage >= totalPages) {
    currentPage = totalPages - 1;
  }

  const int percent = static_cast<int>(((currentPage + 1U) * 100U) / totalPages);
  return std::min(100, std::max(0, percent));
}

int loadTxtProgressPercent(const std::string& path) {
  Txt txt(path, "/.crosspoint");
  // load() ensures cache path generation semantics are consistent with reader.
  if (!txt.load()) {
    return -1;
  }

  uint32_t currentPage = 0;
  FsFile progressFile;
  if (Storage.openFileForRead("RBA", txt.getCachePath() + "/progress.bin", progressFile)) {
    uint8_t data[4];
    if (progressFile.read(data, 4) == 4) {
      currentPage = data[0] + (data[1] << 8);
    }
    progressFile.close();
  }

  FsFile indexFile;
  if (!Storage.openFileForRead("RBA", txt.getCachePath() + "/index.bin", indexFile)) {
    return -1;
  }

  uint32_t magic = 0;
  uint8_t version = 0;
  serialization::readPod(indexFile, magic);
  serialization::readPod(indexFile, version);
  if (magic != TXT_INDEX_MAGIC || version < TXT_INDEX_VERSION_MIN || version > TXT_INDEX_VERSION_MAX) {
    indexFile.close();
    return -1;
  }

  // Skip remaining header fields until numPages.
  uint32_t fileSize = 0;
  int32_t viewportWidth = 0;
  int32_t linesPerPage = 0;
  int32_t fontId = 0;
  int32_t margin = 0;
  uint8_t alignment = 0;
  uint32_t totalPages = 0;
  serialization::readPod(indexFile, fileSize);
  serialization::readPod(indexFile, viewportWidth);
  serialization::readPod(indexFile, linesPerPage);
  serialization::readPod(indexFile, fontId);
  serialization::readPod(indexFile, margin);
  serialization::readPod(indexFile, alignment);
  serialization::readPod(indexFile, totalPages);
  indexFile.close();

  if (totalPages == 0) {
    return -1;
  }
  if (currentPage >= totalPages) {
    currentPage = totalPages - 1;
  }

  const int percent = static_cast<int>(((currentPage + 1U) * 100U) / totalPages);
  return std::min(100, std::max(0, percent));
}

int loadProgressPercentForPath(const std::string& path) {
  if (StringUtils::checkFileExtension(path, ".epub")) {
    return loadEpubProgressPercent(path);
  }
  if (StringUtils::checkFileExtension(path, ".xtch") || StringUtils::checkFileExtension(path, ".xtc")) {
    return loadXtcProgressPercent(path);
  }
  if (StringUtils::checkFileExtension(path, ".txt") || StringUtils::checkFileExtension(path, ".md")) {
    return loadTxtProgressPercent(path);
  }
  return -1;
}
}  // namespace

void RecentBooksActivity::loadRecentBooks() {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(books.size());

  for (const auto& book : books) {
    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }
    recentBooks.push_back(book);
  }
}

void RecentBooksActivity::onEnter() {
  Activity::onEnter();

  // Load data
  loadRecentBooks();
  loadReadingMetrics();

  selectorIndex = 0;
  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
  readingMetrics.clear();
}

std::string RecentBooksActivity::formatDuration(const uint32_t totalSeconds) {
  const uint32_t hours = totalSeconds / 3600U;
  const uint32_t minutes = (totalSeconds % 3600U) / 60U;
  return std::to_string(hours) + "h " + (minutes < 10 ? "0" : "") + std::to_string(minutes) + "m";
}

int RecentBooksActivity::clampProgressPercent(const int percent) { return std::min(100, std::max(0, percent)); }

void RecentBooksActivity::loadReadingMetrics() {
  readingMetrics.clear();
  readingMetrics.reserve(recentBooks.size());

  for (const auto& book : recentBooks) {
    RecentBookReadingMetrics metrics;
    metrics.progressPercent = loadProgressPercentForPath(book.path);
    metrics.readSeconds = loadReadSecondsForPath(book.path);

    if (metrics.progressPercent >= 100) {
      metrics.remainingSeconds = 0;
      metrics.hasRemainingEstimate = true;
    } else if (metrics.progressPercent > 0 && metrics.readSeconds > 0) {
      const float ratio = static_cast<float>(metrics.progressPercent) / 100.0f;
      const float estimateTotal = static_cast<float>(metrics.readSeconds) / ratio;
      const uint32_t estimateTotalSeconds = static_cast<uint32_t>(estimateTotal + 0.5f);
      metrics.remainingSeconds =
          (estimateTotalSeconds > metrics.readSeconds) ? (estimateTotalSeconds - metrics.readSeconds) : 0;
      metrics.hasRemainingEstimate = true;
    }

    readingMetrics.push_back(metrics);
  }
}

void RecentBooksActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!recentBooks.empty() && selectorIndex < static_cast<int>(recentBooks.size())) {
      LOG_DBG("RBA", "Selected recent book: %s", recentBooks[selectorIndex].path.c_str());
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
  }

  int listSize = static_cast<int>(recentBooks.size());

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void RecentBooksActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // Recent tab
  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, recentBooks.size(), selectorIndex,
        [this](int index) { return recentBooks[index].title; },
        [this](int index) {
          if (index < 0 || static_cast<size_t>(index) >= readingMetrics.size()) return std::string();
          const auto& m = readingMetrics[index];
          const std::string percent =
              (m.progressPercent >= 0) ? (std::to_string(clampProgressPercent(m.progressPercent)) + "%") : "--%";
          const std::string readPart = formatDuration(m.readSeconds);
          const std::string leftPart = m.hasRemainingEstimate ? formatDuration(m.remainingSeconds) : std::string("--");
          return percent + " • " + readPart + " • " + leftPart;
        },
        [this](int index) { return UITheme::getFileIcon(recentBooks[index].path); });

    const int rowHeight = metrics.listWithSubtitleRowHeight;
    const int pageItems = (rowHeight > 0) ? (contentHeight / rowHeight) : 0;
    if (pageItems > 0) {
      const int pageStartIndex = (static_cast<int>(selectorIndex) / pageItems) * pageItems;
      const int visibleEnd = std::min(static_cast<int>(recentBooks.size()), pageStartIndex + pageItems);
      const int barX = metrics.contentSidePadding + 18;
      const int barWidth = pageWidth - barX - metrics.contentSidePadding - 26;
      const int barHeight = 4;
      for (int i = pageStartIndex; i < visibleEnd; ++i) {
        const int itemY = contentTop + (i - pageStartIndex) * rowHeight;
        const int barY = itemY + rowHeight - 10;
        const int pct =
            (i >= 0 && static_cast<size_t>(i) < readingMetrics.size() && readingMetrics[i].progressPercent >= 0)
                ? clampProgressPercent(readingMetrics[i].progressPercent)
                : 0;
        const int filledWidth = (barWidth * pct) / 100;
        renderer.drawRect(barX, barY, barWidth, barHeight, true);
        if (filledWidth > 0) {
          renderer.fillRect(barX + 1, barY + 1, std::max(1, filledWidth - 2), std::max(1, barHeight - 2), true);
        }
      }
    }
  }

  // Help text
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
