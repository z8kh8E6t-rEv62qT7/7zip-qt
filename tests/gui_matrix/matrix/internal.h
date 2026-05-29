#pragma once

#include <QtTest/QtTest>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

struct GuiMatrixItem {
  QString behavior_id;
  QString behavior;
  QStringList original_anchor;
  QStringList qt_anchor;
  QString suite;
  QStringList cases;
  QString status;
  QString gate;
};

class GuiBehaviorMatrixTest final : public QObject {
  Q_OBJECT

 private slots:
  void matrixSchemaIsValid();
  void matrixReferencesKnownTestsOnly();
  void matrixCoversAllBehaviorTests();
  void macOSInfoPlistDeclaresArchiveDocumentTypes();
};
