#pragma once

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool handleButtonAction(CrossPointSettings::ButtonAction action) override;

 private:
  void loadSiblingImages();
  // Confirm (or the reader-menu gesture) opens the image menu.
  void openMenu();
  void confirmDelete();
  void deleteCurrentImage();
  std::string currentFileName() const;
  void doSetSleepCover();
  bool canSetSleepCover() const;
  bool renderPng();

  std::string filePath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
};
