/* Copyright 2013 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <QString>
#include "logger/QsLog.h"
#include <QtXml/QtXml>
#include "OneSixAssets.h"
#include "net/NetJob.h"
#include "net/HttpMetaCache.h"
#include "net/S3ListBucket.h"
#include "MultiMC.h"

#define ASSETS_URL "http://resources.download.minecraft.net/"

class ThreadedDeleter : public QThread
{
	Q_OBJECT
public:
	void run()
	{
		QLOG_INFO() << "Cleaning up assets folder...";
		QDirIterator iter(m_base, QDirIterator::Subdirectories);
		int base_length = m_base.length();
		while (iter.hasNext())
		{
			QString filename = iter.next();
			QFileInfo current(filename);
			// we keep the dirs... whatever
			if (current.isDir())
				continue;
			QString trimmedf = filename;
			trimmedf.remove(0, base_length + 1);
			if (m_whitelist.contains(trimmedf))
			{
				QLOG_TRACE() << trimmedf << " gets to live";
			}
			else
			{
				// DO NOT TOLERATE JUNK
				QLOG_TRACE() << trimmedf << " dies";
				QFile f(filename);
				f.remove();
			}
		}
	}
	QString m_base;
	QStringList m_whitelist;
};

void OneSixAssets::downloadFinished()
{
	deleter = new ThreadedDeleter();
	QDir dir("assets");
	deleter->m_base = dir.absolutePath();
	deleter->m_whitelist = nuke_whitelist;
	connect(deleter, SIGNAL(finished()), SIGNAL(finished()));
	deleter->start();
}

void OneSixAssets::S3BucketFinished()
{
	QString prefix(ASSETS_URL);
	nuke_whitelist.clear();

	emit filesStarted();

	auto firstJob = index_job->first();
	auto objectList = std::dynamic_pointer_cast<S3ListBucket>(firstJob)->objects;

	NetJob *job = new NetJob("Assets");

	connect(job, SIGNAL(succeeded()), SLOT(downloadFinished()));
	connect(job, SIGNAL(failed()), SIGNAL(failed()));
	connect(job, SIGNAL(filesProgress(int, int, int)), SIGNAL(filesProgress(int, int, int)));

	auto metacache = MMC->metacache();

	for (auto object : objectList)
	{
		// Filter folder keys (zero size)
		if (object.size == 0)
			continue;

		nuke_whitelist.append(object.Key);

		auto entry = metacache->resolveEntry("assets", object.Key, object.ETag);
		if (entry->stale)
		{
			job->addNetAction(CacheDownload::make(QUrl(prefix + object.Key), entry));
		}
	}
	if (job->size())
	{
		files_job.reset(job);
		files_job->start();
	}
	else
	{
		delete job;
		emit finished();
	}
}

void OneSixAssets::start()
{
	auto job = new NetJob("Assets index");
	job->addNetAction(S3ListBucket::make(QUrl(ASSETS_URL)));
	connect(job, SIGNAL(succeeded()), SLOT(S3BucketFinished()));
	connect(job, SIGNAL(failed()), SIGNAL(failed()));
	emit indexStarted();
	index_job.reset(job);
	job->start();
}

#include "OneSixAssets.moc"
