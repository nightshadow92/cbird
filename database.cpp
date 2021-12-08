/* Database management and search
   Copyright (C) 2021 scrubbbbs
   Contact: screubbbebs@gemeaile.com =~ s/e//g
   Project: https://github.com/scrubbbbs/cbird

   This file is part of cbird.

   cbird is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   cbird is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with cbird; if not, see
   <https://www.gnu.org/licenses/>.  */
#include "database.h"
#include "profile.h"
#include "templatematcher.h"
#include "qtutil.h"

QAtomicInt& Database::connectionCount() {
  static auto* s = new QAtomicInt(0);
  return *s;
}

QHash<int, QHash<QThread*, QString>>& Database::dbConnections() {
  static auto* s = new QHash<int, QHash<QThread*, QString>>;
  return *s;
}

QMutex& Database::dbMutex() {
  static auto* s = new QMutex(QMutex::Recursive);
  return *s;
}

QSqlDatabase Database::connect(int id) {
  QThread* thread = QThread::currentThread();

  QMutexLocker locker(&dbMutex());

  const auto& dbs = dbConnections();
  const auto it = dbs.find(id);
  if (it != dbs.end()) {
    const auto& hash = it.value();
    auto it = hash.find(thread);
    if (it != hash.end()) {
      bool isValid = false;

      // this scope is to keep db reference from crossing disconnect below
      {
        QString currName = "invalid";
        QString reqName = dbPath(id);

        QSqlDatabase db = QSqlDatabase::database(*it);
        if (db.isValid()) {
          isValid = true;

          // check that the database assigned to this thread is the one
          // you wanted. It might not be if using multiple Database instances
          currName = db.databaseName();
          if (currName == reqName) return db;

          qWarning("invalid cached connection: %s (%s), wanted (%s)",
                   qPrintable(*it), qPrintable(currName), qPrintable(reqName));
        }
      }

      // connection is "valid" but not the one we want
      if (isValid) disconnect();
    }
  }

  // each db connection needs a unique identifier; use a counter
  int connId = connectionCount()++;

  QString name = QString("sqlite_%1_%2").arg(id).arg(connId);

  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
  db.setDatabaseName(dbPath(id));
  db.setConnectOptions("QSQLITE_ENABLE_REGEXP=1");

  Q_ASSERT(db.open());
  Q_ASSERT(db.isOpen());
  Q_ASSERT(db.isValid());

  // we'd prefer case-insensitive like for matching file names
  QSqlQuery query(db);
  if (!query.exec("pragma case_sensitive_like = true;")) SQL_FATAL(exec);

  //    qDebug("thread=%p %s %s",
  //        thread,
  //        qPrintable(db.connectionName()),
  //        qPrintable(db.databaseName()));

  dbConnections()[id][thread] = name;

  return db;
}

void Database::disconnect() {
  QThread* thread = QThread::currentThread();

  QMutexLocker locker(&dbMutex());

  auto& dbs = dbConnections();

  // disconnect all dbs the current thread is holding
  for (auto& cons : dbs.values()) {
    auto it = cons.find(thread);
    if (it != cons.end()) {
      QString connName = *it;
      QString dbName = QSqlDatabase::database(connName).databaseName();
      qDebug("thread:%p %s %s", reinterpret_cast<void*>(thread),
             qPrintable(connName), qPrintable(dbName));
      cons.remove(thread);
      // must be last, after cons() gives up its reference
      QSqlDatabase::removeDatabase(connName);
    }
  }
}

void Database::disconnectAll() {
  QMutexLocker locker(&dbMutex());
  auto& dbs = dbConnections();
  for (auto& cons : dbs.values()) {
    for (auto& thread : cons.keys()) {
      QString connName = cons[thread];
      QString dbName = QSqlDatabase::database(connName).databaseName();
      qDebug("thread:%p %s %s", reinterpret_cast<void*>(thread),
             qPrintable(connName), qPrintable(dbName));
      cons.remove(thread);
      QSqlDatabase::removeDatabase(connName);
    }
  }
}

void Database::setup() {
  // sub-databases are in charge of db creation
  for (Index* i : _algos) {
    QSqlDatabase db = connect(i->databaseId());
    i->createTables(db);
  }

  QSqlQuery query(connect());

  //    qDebug("type = \"%s\"", connect().driver()->handle().typeName());

  // create tables for the default sql database (index 0)
  if (!query.exec("select * from media limit 1")) {
    createTables();

    // note: for some reason createTables messes up query, make new one
    query = QSqlQuery(connect());
    if (!query.exec("select * from media limit 1")) SQL_FATAL(exec);
  }

  // example of a database upgrade

  //    if (query.exec("select histogram from media limit 1"))
  //    {
  //        // not going to remove histogram column, too messy
  //        qInfo("adding column media.color_desc");

  //        if (!connect().transaction())
  //            qFatal("update failed (begin transaction): %s",
  //            qPrintable(connect().lastError().text()));

  //        if (!query.exec("alter table media rename to media_old"))
  //            qFatal("update failed (rename table): %s",
  //            qPrintable(query.lastError().text()));

  //        if (!query.exec(
  //            "create table media ("
  //            " id      integer primary key not null,"
  //            " type    integer not null,"
  //            " path    text not null,"
  //            " width   text not null,"
  //            " height  text not null,"
  //            " md5     text not null,"
  //            " phash_dct  integer not null"
  //            " );"))
  //            qFatal("update failed (create table): %s",
  //            qPrintable(query.lastError().text()));

  //        if (!query.exec(
  //            "insert into media (id,type,path,width,height,md5,phash_dct)\n"
  //            "select            id,type,path,width,height,md5,phash_dct from
  //            media_old")) qFatal("update failed (copy table): %s",
  //            qPrintable(query.lastError().text()));

  //        if (!query.exec("drop table media_old"))
  //            qFatal("update failed (drop old table): %s",
  //            qPrintable(query.lastError().text()));

  //        if (!connect().commit())
  //            qFatal("update failed (commit transaction): %s",
  //            qPrintable(connect().lastError().text()));
  //    }

  query.exec("select * from media limit 1");
  QSqlRecord record = query.record();
  _mediaIndex.id = record.indexOf("id");
  _mediaIndex.type = record.indexOf("type");
  _mediaIndex.path = record.indexOf("path");
  _mediaIndex.width = record.indexOf("width");
  _mediaIndex.height = record.indexOf("height");
  _mediaIndex.md5 = record.indexOf("md5");
  _mediaIndex.phash_dct = record.indexOf("phash_dct");

  Q_ASSERT(_mediaIndex.id >= 0);
  Q_ASSERT(_mediaIndex.type >= 0);
  Q_ASSERT(_mediaIndex.path >= 0);
  Q_ASSERT(_mediaIndex.md5 >= 0);
}

void Database::createTables() {
  QSqlQuery query(connect());
  if (!query.exec("create table media ("
                  " id      integer primary key not null,"
                  " type    integer not null,"
                  " path    text not null,"
                  " width   integer not null,"
                  " height  integer not null,"
                  " md5     text not null,"  // fixme: could be number/binary to
                                             // save space
                  " phash_dct  integer not null"
                  " );"))
    SQL_FATAL(exec);

  if (!query.exec("create unique index media_id_index on media(id);"))
    SQL_FATAL(exec);

  if (!query.exec("create unique index media_path_index on media(path);"))
    SQL_FATAL(exec);

  if (!query.exec("create index media_md5_index on media(md5);"))
    SQL_FATAL(exec);

// we don't store keypoints as there is nothing that uses them,
// and they take up a lot of space
#ifdef ENABLE_KEYPOINTS_DB
  Q_ASSERT(
      ("create table keypoint ("
       " id       integer primary key not null,"
       " media_id integer not null,"
       " x        real not null,"
       " y        real not null,"
       " size     real not null,"
       " angle    real,"
       " response real,"
       " octave   real,"
       " class_id integer"
       " );"));

  Q_ASSERT(("create unique index keypoint_id_index on keypoint(id);"));

  Q_ASSERT(("create index keypoint_fk_index on keypoint(media_id);"));
#endif
}

Database::Database(const QString& path_) {
  QDir dir = QDir::current();
  if (path_ != "") dir = QDir(path_);

  if (!dir.exists())
    qFatal("directory does not exist: \"%s\"",
           qUtf8Printable(dir.absolutePath()));
  _indexDir = dir.absolutePath();

  qDebug() << "loading from" << _indexDir;

  // to verify database functionality, check that we have
  // an index for columns of the "media" table (in setup())
  memset(&_mediaIndex, 0xFF, sizeof(_mediaIndex));

  if (!dir.mkpath(indexPath()))
    qFatal("failed to create index folder: \"%s\"",
           qUtf8Printable(indexPath()));

  Q_ASSERT(dir.mkpath(cachePath()));
  Q_ASSERT(dir.mkpath(videoPath()));
}

Database::~Database() {
  qDebug("destruct");

  qInfo("save Indices: start");
  saveIndices();
  qInfo("save Indices: done");

  // close all db connections; hopefully there are no
  // threads running that want the db
  // fixme: why is this commented out?
  // maybe we don't have to do this anymore since
  // connect() will disconnect the old ones?
  /*
      QMutexLocker locker(&_dbMutex);

      for (auto& conns : _dbConnections.values())
      for (QSqlDatabase* db : conns.values())
      {
          qDebug("Database::disconnect: (destruct) %s %s\n",
              qPrintable(db->databaseName()),
              qPrintable(db->connectionName()));

          QString name = db->connectionName();
          db->close();
          delete db;

          // this is last because delete above releases a reference
          QSqlDatabase::removeDatabase(name);
      }

      _dbConnections.clear();
   */
}

QDateTime Database::lastAdded() {
  // note: we cannot use the date of the database file, because
  //       other operations like rename, vacuum will modify it
  QFileInfo info(indexPath() + "/last-added.txt");
  if (!info.exists()) {
    qWarning() << "missing timestamp file, -update may ignore modified files";
    return DBHelper::lastModified(connect());
  }
  return info.lastModified();
}

void Database::writeTimestamp() {
  QFile f(indexPath() + "/last-added.txt");
  Q_ASSERT(f.open(QFile::WriteOnly | QFile::Truncate));
  QString date = QDateTime::currentDateTime().toString();
  f.write(qPrintable(date));
}

void Database::add(MediaGroup& inMedia) {

  uint64_t then = nanoTime();
  uint64_t now;

  // try to protect database from corruption
  // it is still possible to corrupt if another thread crashes the app
  // during a commit.

  QWriteLocker locker(&_rwLock);
  QLockFile dbLock(indexPath() + "/write.lock");
  if (!dbLock.tryLock(0)) {
    qCritical() << "database update aborted, another process is writing,"
                << "or lock file is stale";
    return;
  }

  int mediaId = -1;
  {
    // using sql auto-increment and lastInsertId() is not going to work
    // since we are batching the insert
    QSqlQuery query(connect());
    if (!query.exec("select max(id) from media")) SQL_FATAL(select)
    if (!query.next()) mediaId = 0;
    mediaId = query.value(0).toInt() + 1;
  }

  MediaGroup media;
  for (const Media& m : qAsConst(inMedia)) {
    Q_ASSERT(!m.path().isEmpty());
    Q_ASSERT(!m.md5().isEmpty());
    Q_ASSERT(m.path().startsWith(path()));

    // Avoid violating unique constraint on media.path, and crashing app
    // this is slow, only useful if multiple processes can call add()
    //if (existingPaths.contains(m.path()))
    //  qWarning() << "attempt to add existing path, ignoring" << m.path();
    //else
    media.append(m);
  }

  std::sort(media.begin(), media.end(), [](const Media& a, const Media& b) {
    return a.path() < b.path();
  });

  connect().transaction();
  for (Index* i : _algos) connect(i->databaseId()).transaction();

  now = nanoTime();
  uint64_t w0 = now-then;
  then=now;

  {
    QSqlQuery query(connect());
    if (!query.prepare("insert into media "
                       "(id, type,  path,  width,  height, md5,  phash_dct) values "
                       "(:id, :type, :path, :width, :height,:md5, :phash_dct)"))
      SQL_FATAL(prepare);

    QVariantList id, type, relPath, width, height, md5, dctHash;
    for (Media& m : media) {
      m.setId(mediaId);
      mediaId++;

      id.append(m.id());
      type.append(m.type());
      relPath.append(m.path().mid(path().length() + 1));
      width.append(m.width());
      height.append(m.height());
      md5.append(m.md5());
      dctHash.append(qlonglong(m.dctHash()));

  #ifdef ENABLE_KEYPOINTS_DB
      foreach (const cv::KeyPoint& kp, m.keyPoints()) {
        if (!query.prepare(
                "insert into keypoint "
                "(media_id,  x,  y,  size,  angle,  response,  class_id) values "
                "(:media_id, :x, :y, :size, :angle, :response, :class_id)")) {
          printf("Database::add keypoint: %s\n",
                 qPrintable(query.lastError().text()));
          exit(-1);
        }

        query.bindValue(":media_id", mediaId);
        query.bindValue(":x", kp.pt.x);
        query.bindValue(":y", kp.pt.y);
        query.bindValue(":size", kp.size);
        query.bindValue(":angle", kp.angle);
        query.bindValue(":response", kp.response);
        query.bindValue(":class_id", kp.class_id);
        if (!query.exec()) {
          printf("Database::add keypoint: %s\n",
                 qPrintable(query.lastError().text()));
          exit(-1);
        }
      }
  #endif

      if (m.type() == Media::TypeVideo && !m.videoIndex().isEmpty()) {
        QString indexPath =
            QString("%1/%2.vdx").arg(videoPath()).arg(m.id());
        m.videoIndex().save(indexPath);
      }
    }

    query.bindValue(":id", id);
    query.bindValue(":type", type);
    query.bindValue(":path", relPath);
    query.bindValue(":width", width);
    query.bindValue(":height", height);
    query.bindValue(":md5", md5);
    query.bindValue(":phash_dct", dctHash);

    if (!query.execBatch()) SQL_FATAL(exec)
  }

  inMedia = media;

  now = nanoTime();
  uint64_t w1 = now-then;
  then=now;

  for (Index* i : _algos) {
    QSqlDatabase db = connect(i->databaseId());
    i->addRecords(db, media);
  }

  now = nanoTime();
  uint64_t w2 = now-then;
  then=now;

  for (Index* index : _algos) index->add(media);

  connect().commit();
  for (Index* i : _algos) connect(i->databaseId()).commit();

  writeTimestamp();

  now = nanoTime();
  uint64_t w3 = now-then;
  then=now;

  qDebug("count=%d write=%d+%d+%d+%d=%d ms",
         media.count(),
         (int)(w0/1000000),
              (int)(w1/1000000),
              (int)(w2/1000000),
              (int)(w3/1000000),
              (int)((w0+w1+w2+w3)/1000000));
}

bool Database::setMd5(Media& m, const QString& md5) {
  QSqlQuery query(connect());

  if (!query.prepare("update media set md5=:md5 where id=:id;")) {
    qCritical("sql-prepare: %s", qPrintable(query.lastError().text()));
    return false;
  }

  query.bindValue(":id", m.id());
  query.bindValue(":md5", md5);

  if (!query.exec()) {
    qCritical("sql-exec: %s", qPrintable(query.lastError().text()));
    return false;
  }

  m.setMd5(md5);
  return true;
}

void Database::remove(int id) {
  QVector<int> ids{id};
  remove(ids);
}

void Database::remove(const MediaGroup& group) {
  QVector<int> ids;
  for (const Media& m : group) ids.append(m.id());

  remove(ids);
}

void Database::remove(const QVector<int>& ids) {
  if (ids.size() <= 0) return;
  for (auto& id : ids)
    if (id == 0) {
      qWarning() << "attempt to remove media id=0 ignored";
      return;
    }

  QWriteLocker locker(&_rwLock);
  QLockFile dbLock(indexPath() + "/write.lock");
  if (!dbLock.tryLock(0)) {
    qCritical() << "database update aborted, another process is writing,"
                << "or lock file is stale";
    return;
  }

  QSqlQuery query(connect());

  connect().transaction();

  uint64_t now;
  uint64_t then = nanoTime();

  // todo: vacuum database after a lot of deletions

  // fixme: see if we should delete in reverse order of creation; maybe it
  // fragments the database file less?
#ifdef ENABLE_KEYPOINTS_DB
  for (int id : ids)
    ("delete from keypoint where media_id=" + QString::number(id));

  now = nanoTime();
  qInfo("delete keypoint=%dms", (int)((now - then) / 1000000));
  then = now;
#endif

  for (int id : ids)
    if (!query.exec("delete from media where id=" + QString::number(id)))
      SQL_FATAL(exec);

  now = nanoTime();
  qInfo("<PL>delete media   =%dms", int((now - then) / 1000000));
  then = now;

  qInfo("<PL>committing txn...");
  connect().commit();

  now = nanoTime();
  qInfo("<PL>finished       =%dms", int((now - then) / 1000000));
  then = now;

  for (Index* i : _algos) {
    qInfo("<PL>algo: %d deleting", i->id());

    QSqlDatabase db = connect(i->databaseId());
    if (!db.transaction())
      qFatal("create transaction: %s", qPrintable(db.lastError().text()));

    i->removeRecords(db, ids);

    qInfo("<PL>algo: %d committing...", i->id());
    if (!db.commit())
      qFatal("commit transaction: %s", qPrintable(db.lastError().text()));

    now = nanoTime();

    qInfo("<PL>algo: %d commit=%dms", i->id(), int((now - then) / 1000000));
    then = now;
  }

  // if it's a video, delete the hash file
  // todo: this could be in removeRecords()
  for (int id : ids) {
    QString hashFile =
        QString::asprintf("%s/%d.vdx", qPrintable(videoPath()), id);
    if (QFileInfo::exists(hashFile))
      if (!QFile(hashFile).remove())
        qCritical("failure to delete file %s", qPrintable(hashFile));
  }

  for (Index* i : _algos) i->remove(ids);
}

void Database::vacuum() {
  QWriteLocker locker(&_rwLock);
  QLockFile dbLock(indexPath() + "/write.lock");
  if (!dbLock.tryLock(0)) {
    qCritical() << "database update aborted, another process is writing,"
                << "or lock file is stale";
    return;
  }
  qInfo("vacuum main db");
  const char* sql = "vacuum";
  QSqlQuery query(connect());
  if (!query.exec(sql))
    SQL_FATAL(exec);

  for (Index* i : _algos) {
    qInfo("vaccum algo: %d", i->id());
    QSqlDatabase db = connect(i->databaseId());
    QSqlQuery query(db);
    if (!query.exec(sql))
      SQL_FATAL(exec);
  }
  // there was a bug that caused video index to be orphaned
  const auto files = QDir(videoPath()).entryList({"*.vdx"});
  for (const QString& f : files) {
    bool ok = false;
    int id = f.split(".").first().toInt(&ok);
    if (!ok) continue;
    if (mediaWithId(id).isValid()) continue;
    qInfo() << "orphaned video index" << f;
    if (!QFile(videoPath()+"/"+f).remove())
      qWarning() << "failed to remove" << f;
  }
  // fixme: remove cache/tmp files
}

QString Database::moveFile(const QString& srcPath, const QString& dstDir) {
  const QFileInfo srcInfo(srcPath);
  const QFileInfo dstInfo(dstDir);

  if (!srcInfo.exists()) {
    qWarning() << "move failed: original does not exist:"
               << srcInfo.absoluteFilePath();
    return "";
  }

  if (!dstInfo.exists()) {
    qWarning() << "move failed: destination does not exist:"
               << dstInfo.absoluteFilePath();
    return "";
  }

  if (!dstInfo.isDir()) {
    qWarning("move failed: destination is not a directory");
    return "";
  }

  if (!dstDir.startsWith(path())) {
    qWarning("move failed: destination is not subdir of index dir");
    return "";
  }

  const QString newPath = dstInfo.absoluteFilePath() + "/" + srcInfo.fileName();
  if (QFileInfo(newPath).exists()) {
    qWarning() << "move failed: destination file exists:" << newPath;
    return "";
  }

  if (!QDir().rename(srcPath, newPath)) {
    qWarning("move failed: file system error");
    return "";
  }

  qInfo() << "moved file" << srcPath << "=>" << newPath;

  return newPath;
}

bool Database::move(Media& old, const QString& dstDir) {
  if (old.isArchived()) {
    qWarning("archive member unsupported");
    return false;
  }

  const QString newPath = moveFile(old.path(), dstDir);
  if (newPath.isEmpty()) return false;

  const Media m = mediaWithId(old.id());
  if (!m.isValid()) {
    qWarning() << "skipping update since item is not in database" << m.id();
    old.setPath(newPath);
    return true;
  }

  const QString relPath = newPath.mid(path().length() + 1);

  bool ok = updatePaths({m}, {relPath});
  if (ok) old.setPath(newPath);
  return ok;
}

QString Database::renameFile(const QString& srcPath, const QString& newName) {
  const QFileInfo info(srcPath);

  if (!info.exists()) {
    qWarning() << "original does not exist" << info.fileName();
    return "";
  }

  if (!info.path().startsWith(path())) {
    qWarning("original is not a subfile of index");
    return "";
  }

  QDir parent = info.dir();
  if (parent.exists(newName)) {
    qWarning() << "new name exists " << parent.absoluteFilePath(newName) ;
    return "";
  }

  if (!parent.rename(info.fileName(), newName)) {
    qCritical("file system error");
    return "";
  }

  qInfo() << "renamed file" << info.fileName() << "=>" << newName;

  return parent.absoluteFilePath(newName);
}

bool Database::rename(Media& old, const QString& newName) {
  if (old.isArchived()) {
    qWarning("archive member unsupported");
    return false;
  }

  const QString absNewPath = renameFile(old.path(), newName);
  if (absNewPath.isEmpty()) return false;

  Media m = mediaWithId(old.id());

  if (!m.isValid()) {
    qWarning() << "skipping update since item is not in database" << m.id();
    old.setPath(absNewPath);
    return true;
  }

  const QString relNewPath = absNewPath.mid(path().length() + 1);

  bool ok = updatePaths({m}, {relNewPath});
  if (ok) old.setPath(absNewPath);
  return ok;
}

bool Database::updatePaths(const MediaGroup& group, const QStringList& newPaths) {

  for (auto& path : newPaths)
    if (newPaths.contains("//")) { // relative, canonical path required!
      qCritical() << "invalid path:" << path;
      return false;
    }

  // todo:: write locker
  QSqlDatabase db(connect());
  if (!db.transaction()) qFatal("db.transaction");

  QSqlQuery query(db);

  for (int i = 0; i < group.count(); ++i) {
    const Media& m = group.at(i);
    const QString& newPath = newPaths[i];
    qDebug() << m.path() << "=>" << newPath;

    if (!query.prepare("update media set path=:path where id=:id;")) {
      qCritical() << "prepare failed: %s" << query.lastError().text();
      qInfo() << "rollback:" << db.rollback();
      return false;
    }

    query.bindValue(":id", m.id());
    query.bindValue(":path", newPath);

    if (!query.exec()) {
      qCritical() << "exec failed: %s" << query.lastError().text();
      qInfo() << "rollback:" << db.rollback();
      return false;
    }
  }

  if (!db.commit()) {
    qCritical() << "db.commit" << db.lastError().text();
    return false;
  }

  return true;
}

bool Database::moveDir(const QString& dirPath, const QString& newName) {
  const QFileInfo info(dirPath);
  QDir parent = info.dir();
  // any relative path is presumed to be relative to the index
  const QString absSrc = QDir(path()).absoluteFilePath(dirPath);

  bool isZip = info.isFile() && Media::isArchive(dirPath);

  if ( !info.exists() || !(info.isDir() || isZip) ) {
    qWarning("src doesn't exist or is not a dir/zip");
    return false;
  }

  if (isZip && !Media::isArchive(newName)) {
    qWarning("src is zip but dst is not a zip");
    return false;
  }

  if (!parent.exists()) {
    qWarning("parent dir of src doesn't exist");
    return false;
  }

  if (parent.exists(newName)) {
    qWarning("dst exists");
    return false;
  }

  if (!absSrc.startsWith(path())) {
    qWarning("src is not a subdir of index");
    return false;
  }

  const QString absDst = QDir(path()).absoluteFilePath(newName);
  if (!absDst.startsWith(path())) {
    qWarning() << "dst cannot be a subpath of index:" << absDst;
    return false;
  }

  if (!parent.rename(absSrc, absDst)) {
    qCritical() << "failed to rename dir/zip: filesystem error src=" << absSrc
                << "dst=" << absDst;
    return false;
  }
  qInfo() << "renamed: " << dirPath << "=>" << newName;

  QDir indexDir(path());
  QString oldPrefix = indexDir.relativeFilePath(absSrc);
  QString newPrefix = indexDir.relativeFilePath(absDst);

  // !! if prefix has wildcards we could update invalid items !!
  QString like = oldPrefix;
  like.replace("%", "\\%").replace("_", "\\_");
  like += isZip ? ":%" : "/%";

  MediaGroup oldNames = mediaWithPathLike(like);
  if (oldNames.count() <= 0) return true;

  QStringList newPaths;
  for (auto& m : oldNames)
    newPaths += newPrefix + m.path().mid(absSrc.length());

  return updatePaths(oldNames, newPaths);
}

void Database::fillMediaGroup(QSqlQuery& query, MediaGroup& media, int maxLen) {
  int i = 1;
  while (query.next()) {
    /*
    {
        QSqlRecord rec = query.record();

        Q_ASSERT(rec.indexOf("id") == _mediaIndex.id);
        Q_ASSERT(rec.indexOf("type") == _mediaIndex.type);
        Q_ASSERT(rec.indexOf("path") == _mediaIndex.path);
        Q_ASSERT(rec.indexOf("width") == _mediaIndex.width);
        Q_ASSERT(rec.indexOf("height") == _mediaIndex.height);
        Q_ASSERT(rec.indexOf("md5") == _mediaIndex.md5);
        Q_ASSERT(rec.indexOf("phash_dct") == _mediaIndex.phash_dct);
    }
    */

    int id = query.value(_mediaIndex.id).toInt();
    int type = query.value(_mediaIndex.type).toInt();

    const QString relPath = query.value(_mediaIndex.path).toString();
    Q_ASSERT(!relPath.isEmpty());

    const QString mediaPath = path() + "/" + relPath;

    Media m(mediaPath, type, query.value(_mediaIndex.width).toInt(),
            query.value(_mediaIndex.height).toInt(),
            query.value(_mediaIndex.md5).toString(),
            uint64_t(query.value(_mediaIndex.phash_dct).toLongLong()));

    // qDebug("%s %d %dx%d", qPrintable(mediaPath), (int)m.phashDct(),
    // m.width(), m.height());

    if (m.width() <= 0 || m.height() <= 0)
      qWarning() << "no dimensions: %s" << m.path();

    m.setId(id);
    media.append(m);

    if (maxLen > 0 && i >= maxLen) break;

    if (i++ % 1000 == 0) qInfo("<PL>sql query %d", i);
  }

  if (i > 1000) qInfo("<PL>sql query %d", i);
}

/*
void Database::loadExtraData(MediaGroup& media)
{
    for(int i = 0; i < media.count(); i++)
    {
        Media& m = media[i];

        QSqlQuery query(connect());

#ifdef ENABLE_KEYPOINTS_DB
        Q_ASSERT(query.prepare("select * from keypoint where media_id=:id"));
        query.bindValue(":id", m.id());
        Q_ASSERT(query.exec());
        KeyPointList kp;
        while (query.next())
        {
            cv::KeyPoint k;
            k.pt.x = query.value(_keyPointIndex.x).toFloat();
            k.pt.y = query.value(_keyPointIndex.y).toFloat();
            k.size = query.value(_keyPointIndex.size).toFloat();
            k.angle = query.value(_keyPointIndex.angle).toFloat();
            k.response = query.value(_keyPointIndex.response).toFloat();
            k.octave = query.value(_keyPointIndex.octave).toInt();
            k.class_id = query.value(_keyPointIndex.class_id).toInt();
            kp.push_back(k);
        }
        m.setKeyPoints(kp);
#endif

        Q_ASSERT(query.prepare("select * from matrix where media_id=:id"));
        query.bindValue(":id", m.id());
        Q_ASSERT(query.exec());
        KeyPointDescriptors desc;
        while (query.next())
        {
            int rows, cols, type, stride;
            QByteArray data;
            rows = query.value(_matrixIndex.rows).toInt();
            cols = query.value(_matrixIndex.cols).toInt();
            type = query.value(_matrixIndex.type).toInt();
            stride = query.value(_matrixIndex.stride).toInt();
            data = query.value(_matrixIndex.data).toByteArray();
            data = qUncompress(data);

            //Q_ASSERT(type==5);
            //Q_ASSERT(stride==512);
            loadMatrix(rows, cols, type, stride, data, desc);
            Q_ASSERT(!query.next()); // should only be one record

            Q_ASSERT(desc.type() == type);
            Q_ASSERT(desc.size().width == cols);
            Q_ASSERT(desc.size().height == rows);
        }

        m.setKeyPointDescriptors(desc);

        Q_ASSERT(query.prepare("select * from kphash where media_id=:id"));
        query.bindValue(":id", m.id());
        Q_ASSERT(query.exec());

        KeyPointHashList hashes;
        while (query.next())
            hashes.push_back(
                (uint64_t)query.value(_kphashIndex.phash_dct).toLongLong());

        m.setKeyPointHashes(hashes);

        printf("loading %d\n", i);
    }
}

void Database::loadKeyPoints(Media& m)
{
    QSqlQuery query(connect());
    Q_ASSERT(query.prepare("select * from keypoint where media_id=:id"));
    query.bindValue(":id", m.id());
    Q_ASSERT(query.exec());
    KeyPointList kp;
    while (query.next())
    {
        cv::KeyPoint k;
        k.pt.x = query.value(_keyPointIndex.x).toFloat();
        k.pt.y = query.value(_keyPointIndex.y).toFloat();
        k.size = query.value(_keyPointIndex.size).toFloat();
        k.angle = query.value(_keyPointIndex.angle).toFloat();
        k.response = query.value(_keyPointIndex.response).toFloat();
        k.octave = query.value(_keyPointIndex.octave).toInt();
        k.class_id = query.value(_keyPointIndex.class_id).toInt();
        kp.push_back(k);
    }
    m.setKeyPoints(kp);
}
*/

bool Database::mediaExists(const QString& path) {
  QString relPath = path;
  if (relPath.startsWith(this->path())) relPath = relPath.mid(this->path().length() + 1);

  QSqlQuery query(connect());

  if (!query.prepare("select id from media where path=:path")) SQL_FATAL(prepare);

  query.bindValue(":path", relPath);

  if (!query.exec()) SQL_FATAL(exec);

  return query.next();
}

MediaGroup Database::mediaWithSql(const QString& sql,
                                  const QString& placeholder,
                                  const QVariant& value) {
  QSqlQuery query(connect());

  if (!query.prepare(sql)) SQL_FATAL(prepare)

  if (!placeholder.isEmpty())
    query.bindValue(placeholder, value);

  if (!query.exec()) SQL_FATAL(exec);

  MediaGroup media;
  fillMediaGroup(query, media);
  return media;
}

Media Database::mediaWithId(int id) {
  MediaGroup media = mediaWithSql(
      "select * from media "
      "where id=:id "
      "order by path",
      ":id", id);
  if (media.count() == 1)
    return media[0];
  else
    return Media();
}

Media Database::mediaWithPath(const QString& path) {
  QString relPath = path;
  QFileInfo info(path);
  if (info.exists())
    relPath = info.absoluteFilePath(); // takes care of ./ ../ etc

  if (relPath.startsWith(this->path())) relPath = relPath.mid(this->path().length() + 1);

  MediaGroup media = mediaWithSql(
      "select * from media "
      "where path=:path",
      ":path", relPath);
  if (media.count() == 1)
    return media[0];
  else
    return Media();
}

MediaGroup Database::mediaWithPathLike(const QString& path) {
  QString relPath = path;
  if (relPath.startsWith("/")) relPath = relPath.mid(this->path().length() + 1);

  return mediaWithSql(
      "select * from media "
      "where path like :path escape '\\'",
      ":path", relPath);
}

MediaGroup Database::mediaWithPathRegexp(const QString& exp) {
  return mediaWithSql(
      "select * from media "
      "where path regexp :exp",
      ":exp", exp);
}

MediaGroup Database::mediaWithMd5(const QString& md5) {
  return mediaWithSql(
      "select * from media "
      "where md5=:md5 "
      "order by path",
      ":md5", md5);
}

MediaGroup Database::mediaWithType(int type) {
  return mediaWithSql(
      "select * from media "
      "where type=:type "
      "order by path",
      ":type", type);
}

int Database::countType(int type) {
  QSqlQuery query(connect());
  if (!query.prepare("select count(*) from media "
                     "where type=:type "
                     "order by path"))
    SQL_FATAL(prepare);

  query.bindValue(":type", type);
  if (!query.exec()) SQL_FATAL(exec);
  if (query.next()) return query.value(0).toInt();
  return 0;
}

size_t Database::memoryUsage() const {
  size_t sum = 0;
  for (const Index* i : _algos) sum += i->memoryUsage();
  return sum;
}

int Database::count() {
  QSqlQuery query(connect());
  if (!query.prepare("select count(*) from media")) SQL_FATAL(prepare);
  if (!query.exec()) SQL_FATAL(exec);
  if (query.next()) return query.value(0).toInt();
  return 0;
}

MediaGroup Database::mediaWithIds(const QVector<int>& ids) {
  if (ids.count() <= 0) return MediaGroup();

  if (ids.count() == 1) {
    MediaGroup g;
    g.append(mediaWithId(ids[0]));
    return g;
  }

  // fixme: seems pointless to use prepare here
  // fixme: if ids list is huge we could hit limits?
  QStringList names;
  for (int i = 0; i < ids.count(); i++)
    names.append(":" + QString::number(ids[i]));

  QSqlQuery query(connect());
  if (!query.prepare("select * from media "
                     "where id in (" +
                     names.join(",") +
                     ") "
                     "order by path"))
    SQL_FATAL(prepare);

  for (int i = 0; i < ids.count(); i++) query.bindValue(names[i], ids[i]);

  if (!query.exec()) SQL_FATAL(exec);

  MediaGroup group;
  fillMediaGroup(query, group);

  if (group.count() != ids.count()) qWarning("some ids requested were missing");

  return group;
}

MediaGroupList Database::dupsByMd5(const SearchParams& params) {
  MediaGroupList dups;

  if (params.inSet) {
    QHash<QString, Media> groups;
    for (const Media& m : params.set) groups.insertMulti(m.md5(), m);

    for (auto& key : groups.uniqueKeys()) {
      auto values = groups.values(key);
      if (values.count() > 1) dups.append(values.toVector());
    }
  } else {
    QSqlQuery query(connect());
    if (!query.exec("select md5 from media "
                    "group by md5 "
                    "having count(md5) > 1 "))
      SQL_FATAL(exec);

    while (query.next()) {
      const QString md5 = query.value(0).toString();
      MediaGroup g = mediaWithMd5(md5);
      if (!g.isEmpty()) dups.append(g);
    }
  }

  Media::sortGroupList(dups, "path");

  return dups;
}

bool Database::filterMatch(const SearchParams& params, MediaGroup& match) {
  // remove matches in the "does not match" database
  if (params.negativeMatch) match = filterNegativeMatches(match);

  // only results under path / not under path
  if (params.path != "" && match.count() > 1) {
    MediaGroup tmp;
    tmp.append(match[0]);

    QString prefix = params.path;
    if (!prefix.startsWith(this->path()))
      prefix = this->path() + "/" + params.path;

    for (int i = 1; i < match.count(); i++)
      if ((!params.inPath) ^ match[i].path().startsWith(prefix))
        tmp.append(match[i]);

    match = tmp;
  }

  // remove match if in the same directory/zip as needle
  if (params.filterParent && match.count() > 1) {
    if (match[0].isArchived()) {
      QString parent, tmp;
      match[0].archivePaths(parent, tmp);
      for (int i = 1; i < match.count(); ++i)
        if (match[i].isArchived()) {
          QString p;
          match[i].archivePaths(p, tmp);
          if (p == parent) { match.remove(i); --i; }
        }
    }
    else {
      auto parent = match[0].path().splitRef("/");
      parent.pop_back();
      for (int i = 1; i < match.count(); ++i) {
        auto tmp = match[i].path().splitRef("/");
        tmp.pop_back();
        if (tmp == parent) { match.remove(i); --i; }
      }
    }
  }

  // accept if there are enough matches after filtering
  if (match.count() > params.minMatches) return false;

  return true;
}

void Database::filterMatches(const SearchParams& params,
                             MediaGroupList& matches) {
  // remove duplicate result (same set of images found more than once)
  // e.g. a matches b, b matches a, only include first one
  if (params.filterGroups) {
    MediaGroupList filtered;

    // prevent mixing a=>b with b=>a matches by sorting
    Media::sortGroupList(matches, "path");

    QSet<uint> groupHash;

    for (const MediaGroup& group : matches) {
      QString str;
      MediaGroup copy = group;
      Media::sortGroup(copy, "path");
      for (const Media& m : copy) str += m.path();

      uint hash = qHash(str);
      if (!groupHash.contains(hash)) {
        filtered.append(group);
        groupHash.insert(hash);
      }
    }
    matches = filtered;
  }

  if (params.mergeGroups)
    Media::mergeGroupList(matches);
  else if (params.expandGroups)
    Media::expandGroupList(matches);
}

MediaGroupList Database::similar(const SearchParams& params) {
  qint64 start = QDateTime::currentMSecsSinceEpoch();

  if (params.mirrorMask)
    qWarning() << "reflected images unsupported, use -similar-to";

  // note: if set is provided, it is assumed to contain relevant media type(s)
  // fixme: should be ok to filter set to relevant type of the query index
  MediaGroup haystack;
  if (params.inSet)
    haystack = params.set;
  else {
    // select all media with specified types
    // e.g. for image-only indexes, query type should be images
    //      for video index, query type can be video, image, or (future) audio
    QStringList queryTypes;
    int flags=params.queryTypes;
    int type=1;
    while (flags) {
      if (flags & 1)
        queryTypes << QString::number(type);
      type++;
      flags >>= 1;
    }

    QSqlQuery query(connect());
    query.setForwardOnly(true);
    if (!query.exec("select * from media where type in (" +
                    queryTypes.join(",") + ")"))
      SQL_FATAL(exec);
    fillMediaGroup(query, haystack);
  }
  int haystackSize = haystack.count();

  qDebug("loading index for algo %d", params.algo);
  Index* index = loadIndex(params);
  Index* slice = nullptr;

  // use id map to avoid slow database query in the work item
  QHash<int, Media> idMap;
  for (auto& m : haystack) idMap.insert(m.id(), m);

  // if we are searching a subset, take a slice of the search space
  if (params.inSet) {
    QSet<uint32_t> ids;
    for (const auto& m : params.set) ids.insert(uint32_t(m.id()));

    if (!ids.empty()) {
      slice = index->slice(ids);
      if (slice)
        index = slice;
      else
        qWarning() << "Index::slice unsupported for index" << index->id();
    }

    if (!ids.isEmpty()) slice = index->slice(ids);
    if (slice) index = slice;
    if (slice) qInfo() << "searching slice of" << slice->count();
  }

  qInfo("index loaded in %dms",
        int(QDateTime::currentMSecsSinceEpoch() - start));
  start = QDateTime::currentMSecsSinceEpoch();

  int progressInterval = qBound(1, params.progressInterval, haystackSize / 10);

  const int progressTotal = haystackSize;

  QAtomicInt progress;
  if (!progress.isFetchAndAddNative())
    qWarning() << "QAtomicInt::fetchAndAddNative() unsupported, performance may suffer";

  MediaGroupList results;
  results.resize(progressTotal);

  TemplateMatcher tm;

  QSet<int> skip;
  QMutex mutex;

  QFuture<void> f = QtConcurrent::map(
      haystack, [&idMap, &results, &progress, &tm, progressInterval,
                 progressTotal, params, index, this](const Media& m) {
        MediaGroup result = this->searchIndex(index, m, params, idMap);

        // give each work item a (lockless) way to write results
        int resultIndex = progress.fetchAndAddRelaxed(1);

        if (result.count() > 0) {
          Media needle = m;
          // set the dstIn frame number of the needle
          // to the frame matched in the first search result
          for (const Media& m : result)
            if (m.matchRange().dstIn >= 0) {
              needle.setMatchRange(MatchRange(-1, m.matchRange().srcIn, 1));
              break;
            }

          if (params.templateMatch) tm.match(needle, result, params);

          // needle must be prepended for filtering step
          result.prepend(needle);

          // we reserved the space so we can write without locks
          results[resultIndex] = result;
        }
        if ((resultIndex % progressInterval) == 0)
          qInfo() << "<PL>" << resultIndex << progressTotal;
      });

  f.waitForFinished();
  delete slice;

  qInfo("searched %d items and found %d matches in %dms", haystackSize,
         results.count(), int(QDateTime::currentMSecsSinceEpoch() - start));

  qDebug() << "filter matches";
  start = QDateTime::currentMSecsSinceEpoch();

  MediaGroupList list;
  for (MediaGroup& match : results)
    if (match.count() > 0 && !filterMatch(params, match))
      list.append(match);

  filterMatches(params, list);

  Media::sortGroupList(list, "path");

  qInfo("filtered %d matches to %d in %dms", results.count(), list.count(),
        int(QDateTime::currentMSecsSinceEpoch() - start));
  return list;
}

MediaGroup Database::similarTo(const Media& needle,
                               const SearchParams& params) {
  qint64 start = QDateTime::currentMSecsSinceEpoch();

  Index* index = loadIndex(params);

  QHash<int, Media> idMap;
  Index* slice = nullptr;
  if (params.inSet) {
    QSet<uint32_t> ids;
    for (const auto& m : params.set) ids.insert(uint32_t(m.id()));

    if (!ids.empty()) {
      slice = index->slice(ids);
      if (slice)
        index = slice;
      else
        qWarning() << "Index(algo)::slice unsupported";
    }
  }

  // todo: multicore search, for *huge* indexes it's an issue
  MediaGroup result = searchIndex(index, needle, params, idMap);

  delete slice;

  // needle needs to be first for filter function,
  // but cannot include it in results
  result.prepend(needle);
  int beforeCount = result.count();
  if (filterMatch(params, result)) {
    if (beforeCount > result.count()) qWarning() << "results filtered";
    result.clear();
  }
  if (result.count() > 0) result.removeFirst();

  if (params.verbose)
    qInfo("%d results in %dms", result.count(),
          int(QDateTime::currentMSecsSinceEpoch() - start));

  // set match flags
  for (Media& m : result) {
    m.readMetadata();

    int flags = 0;

    if (m.md5() == needle.md5()) flags |= Media::MatchExact;

    if (m.resolution() < needle.resolution())
      flags |= Media::MatchBiggerDimensions;

    if (m.compressionRatio() > needle.compressionRatio())
      flags |= Media::MatchLessCompressed;

    if (m.originalSize() < needle.originalSize())
      flags |= Media::MatchBiggerFile;

    m.setMatchFlags(flags);
  }

  return result;
}

QSet<QString> Database::indexedFiles() {
  QSet<QString> paths;

  QSqlQuery query(connect());

  if (!query.prepare("select path from media")) SQL_FATAL(prepare);
  if (!query.exec()) SQL_FATAL(exec);

  while (query.next()) {
    const QString relPath = query.value(0).toString();
    Q_ASSERT(!relPath.isEmpty());
    paths.insert(path() + "/" + relPath);
  }

  return paths;
}

void Database::addIndex(Index* index) { _algos.append(index); }

Index* Database::chooseIndex(const SearchParams& params) const {
  for (const Index* i : _algos)
    if (i->id() == params.algo) return const_cast<Index*>(i);

  qFatal("no index with id %d", params.algo);
  Q_UNREACHABLE();
  return nullptr;
}

Index* Database::loadIndex(const SearchParams& params) {
  Index* i = chooseIndex(params);

  if (!i->isLoaded()) {
    QWriteLocker locker(&_rwLock);
    if (!i->isLoaded()) {
      QString dataPath = "";
      if (i->id() == SearchParams::AlgoVideo) dataPath = videoPath();

      QSqlDatabase db = connect(i->databaseId());
      i->load(db, cachePath(), dataPath);
    }
  }

  return i;
}

void Database::saveIndices() {
  for (Index* i : _algos) {
    QSqlDatabase db = connect(i->databaseId());
    i->save(db, cachePath());
  }
}

MediaGroup Database::searchIndex(Index* index, const Media& needle,
                                 const SearchParams& params,
                                 const QHash<int, Media>& subset) {
  QReadLocker locker(&_rwLock);

  QVector<Index::Match> matches = index->find(needle, params);

  // sort matches by score and limit the number returned
  std::sort(matches.begin(), matches.end());

  if (matches.count() > params.maxMatches)
    matches.erase(matches.begin() + params.maxMatches, matches.end());

  MediaGroup group;

  for (const Index::Match& match : matches) {
    // database queries can be expensive, if the media matched
    // itself do not query
    if (params.filterSelf && int(match.mediaId) == needle.id()) continue;

    // static QAtomicInt num;

    Media media;
    if (!subset.isEmpty()) {
      auto ii = subset.find(int(match.mediaId));
      if (ii != subset.end()) media = ii.value();
    } else {
      // qint64 then = QDateTime::currentMSecsSinceEpoch();
      media = mediaWithId(int(match.mediaId));
      // qint64 now = QDateTime::currentMSecsSinceEpoch();
      // qDebug() << "query ms:" << num++ << (now-then);
    }

    if (media.isValid()) {
      (void)index->findIndexData(media);
      media.setScore(match.score);
      media.setMatchRange(match.range);
      group.append(media);
    } else
      qWarning("no media with id: %d, index could be stale or corrupt",
               int(match.mediaId));
  }

  return group;
}

bool Database::isNegativeMatch(const Media& m1, const Media& m2) {
  if (!_negMatchLoaded) loadNegativeMatches();

  QReadLocker locker(&_rwLock);
  auto it = _negMatch.find(m1.md5());
  if (it != _negMatch.end() && it.value().contains(m2.md5())) return true;

  it = _negMatch.find(m2.md5());
  if (it != _negMatch.end() && it.value().contains(m1.md5())) return true;

  return false;
}

MediaGroup Database::filterNegativeMatches(const MediaGroup& group) {
  if (group.count() <= 0) return group;

  const Media& m0 = group[0];
  MediaGroup filtered;
  for (int i = 1; i < group.count(); i++)
    if (!isNegativeMatch(m0, group[i])) filtered.append(group[i]);

  if (filtered.count() > 0) filtered.prepend(m0);

  return filtered;
}

void Database::addNegativeMatch(const Media& m1, const Media& m2) {
  if (isNegativeMatch(m1, m2)) {
    qWarning() << "not adding, duplicate match";
    return;
  }

  if (m1.md5() == m2.md5()) {
    qWarning() << "not adding, exact duplicates";
    return;
  }

  QWriteLocker locker(&_rwLock);
  qDebug() << "adding" << m1.md5() << m2.md5();

  _negMatch[m1.md5()].append(m2.md5());
  _negMatch[m2.md5()].append(m1.md5());

  QFile f(indexPath() + "/neg.dat");
  f.open(QFile::Append);
  f.write((m1.md5() + "," + m2.md5() + "\n").toLatin1());
}

void Database::loadNegativeMatches() {
  QWriteLocker locker(&_rwLock);
  if (_negMatchLoaded) return;

  unloadNegativeMatches();

  QFile f(indexPath() + "/neg.dat");
  f.open(QFile::ReadOnly);
  while (f.bytesAvailable()) {
    QString line = f.readLine(256);
    line = line.trimmed();
    QStringList cols = line.split(",");
    Q_ASSERT(cols.count() == 2);
    _negMatch[cols[0]].append(cols[1]);
    _negMatch[cols[1]].append(cols[0]);
  }
  _negMatchLoaded = true;
}

void Database::unloadNegativeMatches() {
  _negMatch.clear();
  _negMatchLoaded = false;
}
