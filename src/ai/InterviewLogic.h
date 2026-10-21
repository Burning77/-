#pragma once
#include "domain/Types.h"
namespace InterviewLogic {
struct ReviewChunk {
    QList<QJsonObject> sources, questions;
};
QList<ReviewChunk> reviewChunks(const QList<QJsonObject> &questions, const QList<QJsonObject> &segments);
TextRequest extraction(const QJsonObject &session, const QList<QJsonObject> &segments,
                       const ProviderConfig &);
QList<QJsonObject> parseQuestions(const QString &text, const QJsonObject &session,
                                  const QList<QJsonObject> &segments, QString *error);
TextRequest answer(const QJsonObject &session, const QJsonObject &question,
                   const QList<QJsonObject> &segments, const ProviderConfig &);
TextRequest review(const QJsonObject &session, const QList<QJsonObject> &questions,
                   const QList<QJsonObject> &segments, const ProviderConfig &);
QString markdown(const QJsonObject &session, const QList<QJsonObject> &segments,
                 const QList<QJsonObject> &questions, const QList<QJsonObject> &answers,
                 const QList<QJsonObject> &reviews);
} // namespace InterviewLogic
