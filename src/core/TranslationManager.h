#pragma once

#include <QString>
#include <QVector>

class QTranslator;

namespace labelqt::core {

struct ApplicationLanguage {
    QString localeName;
    QString displayName;
};

QVector<ApplicationLanguage> availableApplicationLanguages();
bool loadApplicationTranslator(QTranslator& translator, const QString& localeName);

} // namespace labelqt::core
