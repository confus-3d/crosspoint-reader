#pragma once
#include <I18n.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "RecentBooksStore.h"
#include "util/ButtonNavigator.h"

struct RecentBookReadingMetrics {
  int progressPercent = -1;
  uint32_t readSeconds = 0;
  uint32_t remainingSeconds = 0;
  bool hasRemainingEstimate = false;
};

class RecentBooksActivity final : public Activity {
 private:
  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  // Recent tab state
  std::vector<RecentBook> recentBooks;
  std::vector<RecentBookReadingMetrics> readingMetrics;

  // Callbacks
  const std::function<void(const std::string& path)> onSelectBook;
  const std::function<void()> onGoHome;

  // Data loading
  void loadRecentBooks();
  void loadReadingMetrics();
  static std::string formatDuration(uint32_t totalSeconds);
  static int clampProgressPercent(int percent);

 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const std::function<void()>& onGoHome,
                               const std::function<void(const std::string& path)>& onSelectBook)
      : Activity("RecentBooks", renderer, mappedInput), onSelectBook(onSelectBook), onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
