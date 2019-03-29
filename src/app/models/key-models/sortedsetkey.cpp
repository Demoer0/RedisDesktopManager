#include "sortedsetkey.h"
#include <qredisclient/connection.h>

SortedSetKeyModel::SortedSetKeyModel(
    QSharedPointer<RedisClient::Connection> connection, QByteArray fullPath,
    int dbIndex, long long ttl)
    : KeyModel(connection, fullPath, dbIndex, ttl, "ZCARD",
               "ZRANGE WITHSCORES") {}

QString SortedSetKeyModel::type() { return "zset"; }

QStringList SortedSetKeyModel::getColumnNames() {
  return QStringList() << "row"
                       << "value"
                       << "score";
}

QHash<int, QByteArray> SortedSetKeyModel::getRoles() {
  QHash<int, QByteArray> roles;
  roles[Roles::RowNumber] = "row";
  roles[Roles::Value] = "value";
  roles[Roles::Score] = "score";
  return roles;
}

QVariant SortedSetKeyModel::getData(int rowIndex, int dataRole) {
  if (!isRowLoaded(rowIndex)) return QVariant();

  QPair<QByteArray, QByteArray> row = m_rowsCache[rowIndex];

  if (dataRole == Roles::Value)
    return row.first;
  else if (dataRole == Roles::Score)
    return row.second.toDouble();
  else if (dataRole == Roles::RowNumber)
    return rowIndex + 1;

  return QVariant();
}

void SortedSetKeyModel::updateRow(int rowIndex, const QVariantMap &row, Callback) {
  if (!isRowLoaded(rowIndex) || !isRowValid(row)) {
    emit m_notifier->error(QCoreApplication::translate("RDM", "Invalid row"));
    return;
  }

  QPair<QByteArray, QByteArray> cachedRow = m_rowsCache[rowIndex];

  bool valueChanged = cachedRow.first != row["value"].toString();
  bool scoreChanged = cachedRow.second != row["score"].toString();

  QPair<QByteArray, QByteArray> newRow(
      (valueChanged) ? row["value"].toByteArray() : cachedRow.first,
      (scoreChanged) ? row["score"].toByteArray() : cachedRow.second);

  // TODO (uglide): Update only score if value not changed

  deleteSortedSetRow(cachedRow.first);
  addSortedSetRow(newRow.first, newRow.second);
  m_rowsCache.replace(rowIndex, newRow);
}

void SortedSetKeyModel::addRow(const QVariantMap &row, Callback) {
  if (!isRowValid(row)) {
    emit m_notifier->error(QCoreApplication::translate("RDM", "Invalid row"));
    return;
  }

  QPair<QByteArray, QByteArray> cachedRow(row["value"].toByteArray(),
                                          row["score"].toByteArray());

  if (addSortedSetRow(cachedRow.first, cachedRow.second)) {
    m_rowsCache.push_back(cachedRow);
    m_rowCount++;
  }
}

void SortedSetKeyModel::removeRow(int i, Callback) {
  if (!isRowLoaded(i)) return;

  QByteArray value = m_rowsCache[i].first;

  try {
    m_connection->commandSync({"ZREM", m_keyFullPath, value}, m_dbIndex);
  } catch (const RedisClient::Connection::Exception &e) {
    emit m_notifier->error(
        QCoreApplication::translate("RDM", "Connection error: ") +
        QString(e.what()));
    return;
  }

  m_rowCount--;
  m_rowsCache.removeAt(i);
  setRemovedIfEmpty();
}

bool SortedSetKeyModel::addSortedSetRow(const QByteArray &value,
                                        QByteArray score) {
  RedisClient::Response result;
  try {
    result = m_connection->commandSync({"ZADD", m_keyFullPath, score, value},
                                       m_dbIndex);
  } catch (const RedisClient::Connection::Exception &e) {
    emit m_notifier->error(
        QCoreApplication::translate("RDM", "Connection error: ") +
        QString(e.what()));
    return false;
  }

  return result.value().toInt() == 1;
}

void SortedSetKeyModel::deleteSortedSetRow(const QByteArray &value) {
  try {
    m_connection->commandSync({"ZREM", m_keyFullPath, value}, m_dbIndex);
  } catch (const RedisClient::Connection::Exception &e) {
    emit m_notifier->error(
        QCoreApplication::translate("RDM", "Connection error: ") +
        QString(e.what()));
  }
}

void SortedSetKeyModel::addLoadedRowsToCache(const QVariantList &rows,
                                             QVariant rowStartId) {
  QList<QPair<QByteArray, QByteArray>> result;

  for (QVariantList::const_iterator item = rows.begin(); item != rows.end();
       ++item) {
    QPair<QByteArray, QByteArray> value;
    value.first = item->toByteArray();
    ++item;

    if (item == rows.end()) {
      emit m_notifier->error(QCoreApplication::translate(
          "RDM", "Data was loaded from server partially."));
      return;
    }

    value.second = item->toByteArray();
    result.push_back(value);
  }

  auto rowStart = rowStartId.toULongLong();
  m_rowsCache.addLoadedRange({rowStart, rowStart + result.size() - 1}, result);
}
