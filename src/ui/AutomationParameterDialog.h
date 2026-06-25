#pragma once

#include "core/AppPreferences.h"
#include "services/AutomationService.h"

#include <QJsonObject>
#include <QMap>

#include <optional>

class QWidget;

class AutomationParameterDialog final {
public:
    struct Values {
        QJsonObject parameters;
        QMap<QString, QString> secrets;
    };

    static std::optional<Values> getValues(QWidget* parent, const labelqt::services::AutomationScript& script,
                                           const QStringList& groups,
                                           const QVector<labelqt::core::LabelGroupStyle>& groupStyles);
};
