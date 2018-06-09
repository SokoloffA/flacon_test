/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * Flacon - audio File Encoder
 * https://github.com/flacon/flacon
 *
 * Copyright: 2012-2015
 *   Alexander Sokoloff <sokoloff.a@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * END_COMMON_COPYRIGHT_HEADER */


#include "cue.h"
#include "types.h"
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QDebug>


enum CueTagId
{
    CTAG_UNKNOWN,
    CTAG_FILE,
    CTAG_TRACK,
    CTAG_INDEX,
    CTAG_DISCID,
    CTAG_CATALOG,
    CTAG_CDTEXTFILE,
    CTAG_TITLE,
    CTAG_COMMENT,
    CTAG_DATE,
    CTAG_FLAGS,
    CTAG_GENRE,
    CTAG_ISRC,
    CTAG_PERFORMER,
    CTAG_SONGWRITER
};


/************************************************

 ************************************************/
QString cueIndexTagKey(int indexNum)
{
    return QString("CUE_INDEX:%1").arg(indexNum);
}


/************************************************

 ************************************************/
CueIndex::CueIndex(const QString &str):
    mNull(true),
    mCdValue(0),
    mHiValue(0)
{
    if (!str.isEmpty())
        mNull = !parse(str);
}


/************************************************

 ************************************************/
QString CueIndex::toString(bool cdQuality) const
{
    if (cdQuality)
    {
        int min =  mCdValue / (60 * 75);
        int sec = (mCdValue - min * 60 * 75) / 75;
        int frm =  mCdValue - (min * 60 + sec) * 75;

        return QString("%1:%2:%3")
                .arg(min, 2, 10, QChar('0'))
                .arg(sec, 2, 10, QChar('0'))
                .arg(frm, 2, 10, QChar('0'));
    }
    else
    {
        int min = mHiValue / (60 * 1000);
        int sec = (mHiValue - min * 60 * 1000) / 1000;
        int msec = mHiValue - (min * 60 + sec) * 1000;

        return QString("%1:%2.%3")
                .arg(min,  2, 10, QChar('0'))
                .arg(sec,  2, 10, QChar('0'))
                .arg(msec, 3, 10, QChar('0'));
    }

}


/************************************************

 ************************************************/
CueIndex CueIndex::operator -(const CueIndex &other) const
{
    CueIndex res;
    res.mCdValue = this->mCdValue - other.mCdValue;
    res.mHiValue = this->mHiValue - other.mHiValue;
    res.mNull = false;
    return res;
}


/************************************************

 ************************************************/
bool CueIndex::operator ==(const CueIndex &other) const
{
    return this->mHiValue == other.mHiValue;
}


/************************************************

 ************************************************/
bool CueIndex::operator !=(const CueIndex &other) const
{
    return this->mHiValue != other.mHiValue;
}


/************************************************

 ************************************************/
bool CueIndex::parse(const QString &str)
{
    QStringList sl = str.split(QRegExp("\\D"), QString::KeepEmptyParts);

    if (sl.length()<3)
        return false;

    bool ok;
    int min = sl[0].toInt(&ok);
    if (!ok)
        return false;

    int sec = sl[1].toInt(&ok);
    if (!ok)
        return false;

    int frm = sl[2].leftJustified(2, '0').toInt(&ok);
    if (!ok)
        return false;

    int msec = sl[2].leftJustified(3, '0').toInt(&ok);
    if (!ok)
        return false;

    mCdValue = (min * 60 + sec) * 75 + frm;
    mHiValue = (min * 60 + sec) * 1000 + msec;
    return true;
}


/************************************************
 *
 ************************************************/
QByteArray unQuote(const QByteArray &line)
{
    if (line.length() > 2 &&
       (line.at(0) == '"' || line.at(0) == '\'') &&
        line.at(0) == line.at(line.length()-1))
    {
        return line.mid(1, line.length() - 2);
    }
    return line;
}


/************************************************

 ************************************************/
CueReader::CueReader(const QString &fileName):
    mFileName(fileName),
    mValid(false)
{
    QFileInfo fi(mFileName);
    if (!fi.exists())
    {
        mErrorString = QObject::tr("File <b>\"%1\"</b> does not exist").arg(mFileName);
        return;
    }

    QFile file(fi.canonicalFilePath());
    if (!file.open(QIODevice::ReadOnly))
    {
        mErrorString = file.errorString();
        return;
    }

    // Detect codepage and skip BOM .............
    QByteArray magic = file.read(3);
    int seek = 0;
    if (magic.startsWith("\xEF\xBB\xBF"))
    {
        mCodecName = "UTF-8";
        seek = 3;
    }

    else if (magic.startsWith("\xFE\xFF"))
    {
        mCodecName = "UTF-16BE";
        seek = 2;
    }

    else if (magic.startsWith("\xFF\xFE"))
    {
        mCodecName = "UTF-16LE";
        seek = 2;
    }

    file.seek(seek);
    // Detect codepage and skip BOM .............

    QByteArrayList data;

    while (!file.atEnd())
    {
        QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;

        data << line;
    }
    file.close();

    mValid = parse(data);

    if (mDisks.isEmpty())
    {
        mValid = false;
        mErrorString = QObject::tr("<b>%1</b> is not a valid cue file. The cue sheet has no FILE tag.").arg(mFileName);
    }

    int startTrackNum = 1;
    for (int i=0; i<mDisks.count(); ++i)
    {
        if (disk(i).tracksCount() == 0)
        {
            mValid = false;
            mErrorString = QObject::tr("<b>%1</b> is not a valid cue file. Disk %2 has no tags.").arg(mFileName).arg(i);
            break;
        }

        mDisks[i].setDiskTag(START_TRACK_NUM, QString("%1").arg(startTrackNum).toLatin1());
        startTrackNum += disk(i).tracksCount();
    }
}


/************************************************

 ************************************************/
QByteArray leftPart(const QByteArray &line, const QChar separator)
{
    int n = line.indexOf(separator);
    if (n > -1)
        return line.left(n);
    else
        return line;
}


/************************************************

 ************************************************/
QByteArray rightPart(const QByteArray &line, const QChar separator)
{
    int n = line.indexOf(separator);
    if (n > -1)
        return line.right(line.length() - n - 1);
    else
        return QByteArray();
}


/************************************************

 ************************************************/
void parseLine(const QByteArray &line, CueTagId &tag, QByteArray &value)
{
    QByteArray l = line.trimmed();

    if (l.isEmpty())
        tag = CTAG_UNKNOWN;


    QByteArray tagStr = leftPart(l, ' ').toUpper();
    value = rightPart(l, ' ').trimmed();


    if (tagStr == "REM")
    {
        tagStr = leftPart(value, ' ').toUpper();
        value = rightPart(value, ' ').trimmed();
    }

    value = unQuote(value);

    //=============================
    if      (tagStr == "FILE")       tag = CTAG_FILE;
    else if (tagStr == "TRACK")      tag = CTAG_TRACK;
    else if (tagStr == "INDEX")      tag = CTAG_INDEX;
    else if (tagStr == "DISCID")     tag = CTAG_DISCID;
    else if (tagStr == "CATALOG")    tag = CTAG_CATALOG;
    else if (tagStr == "CDTEXTFILE") tag = CTAG_CDTEXTFILE;
    else if (tagStr == "TITLE")      tag = CTAG_TITLE;
    else if (tagStr == "COMMENT")    tag = CTAG_COMMENT;
    else if (tagStr == "DATE")       tag = CTAG_DATE;
    else if (tagStr == "FLAGS")      tag = CTAG_FLAGS;
    else if (tagStr == "GENRE")      tag = CTAG_GENRE;
    else if (tagStr == "ISRC")       tag = CTAG_ISRC;
    else if (tagStr == "PERFORMER")  tag = CTAG_PERFORMER;
    else if (tagStr == "SONGWRITER") tag = CTAG_SONGWRITER;
    else                             tag = CTAG_UNKNOWN;
}


/************************************************

 ************************************************/
QByteArray extractFileFromFileTag(const QByteArray &value)
{
    int n = value.lastIndexOf(' ');
    if (n>-1)
        return unQuote(value.left(n));

    return unQuote(value);
}


/************************************************
 Complete cue sheet syntax documentation
 https://github.com/flacon/flacon/blob/master/cuesheet_syntax.md
 ************************************************/
bool CueReader::parse(const QByteArrayList &data)
{
    QByteArray performer;
    QByteArray album;
    QByteArray genre;
    QByteArray date;
    QByteArray comment;
    QByteArray songWriter;
    QByteArray diskId;
    QByteArray catalog;
    QByteArray cdTextFile;
    QByteArray file;

    enum class Mode {
        Global,
        Track
    };

    Mode mode = Mode::Global;

    DiskNum  diskNum = 0;
    int trackNum = 0;
    QList<CueTagSet>::iterator tags;


    int lineNum = 0;
    auto i = data.begin();
    for (; i != data.end(); ++i)
    {
        lineNum++;
        CueTagId tag;
        QByteArray value;
        parseLine((*i), tag, value);

        if (tag == CTAG_FILE)
        {
            ++diskNum;
            trackNum = 0;
            file = extractFileFromFileTag(value);
            mDisks << CueTagSet(QFileInfo(mFileName).absoluteFilePath() + QString(" [%1]").arg(diskNum));
            tags = --(mDisks.end());
            if (!mCodecName.isEmpty())
                tags->setTextCodecName(mCodecName);

            mode = Mode::Global;
            continue;
        }

        if (tag == CTAG_TRACK)
        {
            trackNum++;

            // Set default values;
            tags->setTrackTag(trackNum-1, TAG_FILE,       file,       false);
            tags->setTrackTag(trackNum-1, TAG_PERFORMER,  performer,  false);
            tags->setTrackTag(trackNum-1, TAG_ALBUM,      album,      false);
            tags->setTrackTag(trackNum-1, TAG_GENRE,      genre,      false);
            tags->setTrackTag(trackNum-1, TAG_DATE,       date,       false);
            tags->setTrackTag(trackNum-1, TAG_COMMENT,    comment,    false);
            tags->setTrackTag(trackNum-1, TAG_SONGWRITER, songWriter, false);
            tags->setDiskTag(TAG_DISCID,     diskId,     true);
            tags->setDiskTag(TAG_CATALOG,    catalog,    true);
            tags->setDiskTag(TAG_CDTEXTFILE, cdTextFile, true);
            mode = Mode::Track;
            continue;
        }

        if (mode == Mode::Global) // Per disk tags ........
        {
            switch(tag)
            {
            case CTAG_DISCID:      diskId = value;     break;
            case CTAG_CATALOG:     catalog = value;    break;
            case CTAG_CDTEXTFILE:  cdTextFile = value; break;
            case CTAG_TITLE:       album = value;      break;
            case CTAG_COMMENT:     comment = value;    break;
            case CTAG_DATE:        date = value;       break;
            case CTAG_GENRE:       genre = value;      break;
            case CTAG_PERFORMER:   performer = value;  break;
            case CTAG_SONGWRITER:  songWriter = value; break;
            default: break;
            }
            continue;
        }
        else // Per track tags ............................
        {

            if (trackNum < 1)
            {
                mErrorString = QObject::tr("<b>%1</b> is not a valid cue file. Incorrect track index on line %2.", "Cue parser error.").arg(mFileName).arg(lineNum);
                return false;
            }

            switch (tag)
            {
            case CTAG_INDEX:
            {
                bool ok;
                int num = leftPart(value, ' ').toInt(&ok);
                if (!ok)
                {
                    mErrorString = QObject::tr("<b>%1</b> is not a valid cue file. Incorrect track index on line %2.", "Cue parser error.").arg(mFileName).arg(lineNum);
                    return false;
                }

                if (num < 0 || num > 99)
                {
                    mErrorString = QObject::tr("<b>%1</b> is not a valid cue file. Incorrect track index on line %2.", "Cue parser error.").arg(mFileName).arg(lineNum);
                    return false;
                }

                tags->setTrackTag(trackNum-1, cueIndexTagKey(num), rightPart(value, ' '));
                break;
            }



            case CTAG_DISCID:      tags->setDiskTag(TAG_DISCID,     value, true); break;
            case CTAG_CATALOG:     tags->setDiskTag(TAG_CATALOG,    value, true); break;
            case CTAG_CDTEXTFILE:  tags->setDiskTag(TAG_CDTEXTFILE, value, true); break;

            case CTAG_TITLE:       tags->setTrackTag(trackNum-1, TAG_TITLE,      value, false); break;
            case CTAG_COMMENT:     tags->setTrackTag(trackNum-1, TAG_COMMENT,    value, false); break;
            case CTAG_DATE:        tags->setTrackTag(trackNum-1, TAG_DATE,       value, false); break;
            case CTAG_GENRE:       tags->setTrackTag(trackNum-1, TAG_GENRE,      value, false); break;
            case CTAG_PERFORMER:   tags->setTrackTag(trackNum-1, TAG_PERFORMER,  value, false); break;
            case CTAG_SONGWRITER:  tags->setTrackTag(trackNum-1, TAG_SONGWRITER, value, false); break;
            case CTAG_ISRC:        tags->setTrackTag(trackNum-1, TAG_ISRC,       value, true);  break;
            case CTAG_FLAGS:       tags->setTrackTag(trackNum-1, TAG_FLAGS,      value, true);  break;

            case CTAG_FILE:
            case CTAG_TRACK:
            case CTAG_UNKNOWN:
                break;
            }
        }
    }

    return true;
}


/************************************************

 ************************************************/
CueTagSet::CueTagSet(const QString &uri):
    TagSet(uri)
{
}


/************************************************

 ************************************************/
CueTagSet::CueTagSet(const CueTagSet &other):
    TagSet(other)
{
}


/************************************************

 ************************************************/
QString CueTagSet::cueFileName() const
{
    return diskTag(TAG_CUE_FILE);
}


/************************************************

 ************************************************/
QString CueTagSet::fileTag() const
{
    return diskTag(TAG_FILE);
}


/************************************************

 ************************************************/
CueIndex CueTagSet::index(int track, int indexNum) const
{
    return CueIndex(trackTag(track, cueIndexTagKey(indexNum)));
}


/************************************************

 ************************************************/
bool CueTagSet::isMultiFileCue() const
{
    return (diskTag("MULTI_FILE") != "");
}


/************************************************

 ************************************************/
int CueTagSet::diskNumInCue() const
{
    return diskTag(TAG_DISKNUM).toInt();
}


