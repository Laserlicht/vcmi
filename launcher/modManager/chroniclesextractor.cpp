/*
 * chroniclesextractor.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "chroniclesextractor.h"

#include "../../lib/VCMIDirs.h"
#include "../../lib/filesystem/CArchiveLoader.h"

#ifdef ENABLE_INNOEXTRACT
#include "cli/extract.hpp"
#include "setup/version.hpp"
#endif

ChroniclesExtractor::ChroniclesExtractor(QWidget *p, std::function<void(float percent)> cb) :
	parent(p), cb(cb)
{
}

bool ChroniclesExtractor::handleTempDir(bool create)
{
	if(create)
	{
		tempDir = QDir(pathToQString(VCMIDirs::get().userDataPath()));
		if(tempDir.cd("tmp"))
		{
			tempDir.removeRecursively(); // remove if already exists (e.g. previous run)
			tempDir.cdUp();
		}
		tempDir.mkdir("tmp");
		if(!tempDir.cd("tmp"))
			return false; // should not happen - but avoid deleting wrong folder in any case
	}
	else
		tempDir.removeRecursively();

	return true;
}

int ChroniclesExtractor::getChronicleNo(QFile & file)
{
	if(!file.open(QIODevice::ReadOnly))
	{
		QMessageBox::critical(parent, tr("File cannot opened"), file.errorString());
		return 0;
	}

	QByteArray magic{"MZ"};
	QByteArray magicFile = file.read(magic.length());
	if(!magicFile.startsWith(magic))
	{
		QMessageBox::critical(parent, tr("Invalid file selected"), tr("You have to select an gog installer file!"));
		return 0;
	}

	QByteArray dataBegin = file.read(1'000'000);
	int chronicle = 0;
	for (const auto& kv : chronicles) {
		if(dataBegin.contains(kv.second))
		{
			chronicle = kv.first;
			break;
		}
	}
	if(!chronicle)
	{
		QMessageBox::critical(parent, tr("Invalid file selected"), tr("You have to select an chronicle installer file!"));
		return 0;
	}
	return chronicle;
}

bool ChroniclesExtractor::extractGogInstaller(QString file)
{
#ifndef ENABLE_INNOEXTRACT
		QMessageBox::critical(parent, tr("Innoextract functionality missing"), "VCMI was compiled without innoextract support, which is needed to extract chroncles!");
		return false;
#else
		::extract_options o;
		o.extract = true;

		// standard settings
		o.gog_galaxy = true;
		o.codepage = 0U;
		o.output_dir = tempDir.path().toStdString();
		o.extract_temp = true;
		o.extract_unknown = true;
		o.filenames.set_expand(true);

		o.preserve_file_times = true; // also correctly closes file -> without it: on Windows the files are not written completely

		QString errorText = "";
		try
		{
			process_file(file.toStdString(), o, [this](float progress) {
				float overallProgress = ((1.0 / static_cast<float>(fileCount)) * static_cast<float>(extractionFile)) + (progress / static_cast<float>(fileCount));
				if(cb)
					cb(overallProgress);
			});
		}
		catch(const std::ios_base::failure & e)
		{
			errorText = tr("Stream error while extracting files!\nerror reason: ");
			errorText += e.what();
		}
		catch(const format_error & e)
		{
			errorText = e.what();
		}
		catch(const std::runtime_error & e)
		{
			errorText = e.what();
		}
		catch(const setup::version_error &)
		{
			errorText = tr("Not a supported Inno Setup installer!");
		}

		if(!errorText.isEmpty())
		{
			QMessageBox::critical(parent, tr("Extracting error!"), errorText);
			return false;
		}

		return true;
#endif
}

void ChroniclesExtractor::createBaseMod() const
{
	QDir dir(pathToQString(VCMIDirs::get().userDataPath() / "Mods"));
	dir.mkdir("chronicles");
	dir.cd("chronicles");
	dir.mkdir("Mods");

	auto [mod, chroniclesTarnum] = createModJsons();

	QFile jsonFile(dir.filePath("mod.json"));
    jsonFile.open(QFile::WriteOnly);
    jsonFile.write(QJsonDocument(mod).toJson());

	dir.mkdir("content");
	dir.mkdir("content/config");
	QFile jsonFileConfig(dir.filePath("content/config/chroniclesTarnum.json"));
    jsonFileConfig.open(QFile::WriteOnly);
    jsonFileConfig.write(QJsonDocument(chroniclesTarnum).toJson());
}

std::tuple<QJsonObject, QJsonObject> ChroniclesExtractor::createModJsons() const
{
	QJsonObject mod
	{
		{ "modType", "Expansion" },
		{ "name", "Heroes Chronicles" },
		{ "description", "Heroes Chronicles" },
		{ "author", "3DO" },
		{ "version", "1.0" },
		{ "contact", "vcmi.eu" },
		{ "settings", QJsonObject{{
				"mapFormat", QJsonObject{{
					"chronicles", QJsonObject{{
						{ "portraits", QJsonObject{{
							{ "portraitTarnumBarbarian", 163 },
							{ "portraitTarnumKnight", 164 },
							{ "portraitTarnumWizard", 165 },
							{ "portraitTarnumRanger", 166 },
							{ "portraitTarnumOverlord", 167 },
							{ "portraitTarnumBeastmaster", 168 }
						}} }
					}}
				}}
		  	}}
		},
		{ "heroes", QJsonArray{"config/chroniclesTarnum.json"} }
	};

	auto fillJson = [](int i){
		std::vector<std::vector<QString>> dataToFill = {
			{"portraitTarnumBarbarian", "barbarian", "Hc_HPL137", "Hc_HPS137", "goblin"},
			{"portraitTarnumKnight", "knight", "Hc_HPL138", "Hc_HPS138", "pikeman"},
			{"portraitTarnumWizard", "wizard", "Hc_HPL139", "Hc_HPS139", "enchanter"},
			{"portraitTarnumRanger", "ranger", "Hc_HPL140", "Hc_HPS140", "sharpshooter"},
			{"portraitTarnumOverlord", "overlord", "Hc_HPL141", "Hc_HPS141", "troglodyte"},
			{"portraitTarnumBeastmaster", "beastmaster", "Hc_HPL142", "Hc_HPS142", "gnoll"}
		};

		QJsonObject chroniclesTarnum
		{
			{ dataToFill[i][0], QJsonObject{{
				{ "class", dataToFill[i][1] },
				{ "special", true },
				{ "images", QJsonObject{{
						{"large", dataToFill[i][2]},
						{"small", dataToFill[i][3]},
						{"specialtySmall", "default"},
						{"specialtyLarge", "default"}
					}}
				},
				{ "texts", QJsonObject{{
						{"name", ""},
						{"biography", ""},
						{"specialty", QJsonObject{{
								{"description", ""},
								{"tooltip", ""},
								{"name", ""}
							}}
						}
					}}
				},
				{ "army", QJsonArray{QJsonObject{{
						{"creature", dataToFill[i][4]},
						{"min", 1},
						{"max", 1}
					}}}
				},
				{ "skills", QJsonArray() },
				{ "specialty", QJsonObject() }
			}}}
		};

		return chroniclesTarnum;
	};

	QVariantMap map = fillJson(0).toVariantMap();
	for(int i = 1; i < 6; i++)
		map.insert(fillJson(i).toVariantMap());

	return { mod, QJsonObject::fromVariantMap(map) };
}

void ChroniclesExtractor::createChronicleMod(int no)
{
	QDir dir(pathToQString(VCMIDirs::get().userDataPath() / "Mods" / "chronicles" / "Mods" / ("chronicles_" + std::to_string(no))));
	dir.removeRecursively();
	dir.mkpath(".");

	QJsonObject mod
	{
		{ "modType", "Expansion" },
		{ "name", "Heroes Chronicles - " + QString::number(no) },
		{ "description", "Heroes Chronicles - " + QString::number(no) },
		{ "author", "3DO" },
		{ "version", "1.0" },
		{ "contact", "vcmi.eu" },
	};
	
	QFile jsonFile(dir.filePath("mod.json"));
    jsonFile.open(QFile::WriteOnly);
    jsonFile.write(QJsonDocument(mod).toJson());

	dir.cd("content");
	
	extractFiles(no);
}

void ChroniclesExtractor::extractFiles(int no) const
{
	QByteArray tmpChronicles = chronicles.at(no);
	tmpChronicles.replace('\0', "");

	QDir tmpDir = tempDir.filePath(tempDir.entryList({"app"}, QDir::Filter::Dirs).front());
	tmpDir.setPath(tmpDir.filePath(tmpDir.entryList({QString(tmpChronicles)}, QDir::Filter::Dirs).front()));
	tmpDir.setPath(tmpDir.filePath(tmpDir.entryList({"data"}, QDir::Filter::Dirs).front()));
	auto basePath = VCMIDirs::get().userDataPath() / "Mods" / "chronicles" / "Mods" / ("chronicles_" + std::to_string(no)) / "content";
	QDir outDirData(pathToQString(basePath / "Data"));
	QDir outDirSprites(pathToQString(basePath / "Sprites"));
	QDir outDirVideo(pathToQString(basePath / "Video"));
	QDir outDirSounds(pathToQString(basePath / "Sounds"));
	QDir outDirMaps(pathToQString(basePath / "Maps"));

	auto extract = [](QDir scrDir, QDir dest, QString file, std::vector<std::string> files = {}){
		CArchiveLoader archive("", scrDir.filePath(scrDir.entryList({file}).front()).toStdString(), false);
		for(auto & entry : archive.getEntries())
			if(files.empty())
				archive.extractToFolder(dest.absolutePath().toStdString(), "", entry.second, true);
			else
			{
				for(const auto & item : files)
					if(!boost::algorithm::to_lower_copy(entry.second.name).find(boost::algorithm::to_lower_copy(item)))
						archive.extractToFolder(dest.absolutePath().toStdString(), "", entry.second, true);
			}
	};
	auto rename = [no](QDir dest){
		dest.refresh();
		for(const auto & entry : dest.entryList())
		{
			if(entry.toUpper().startsWith("HPS") || entry.toUpper().startsWith("HPL"))
				dest.rename(entry, "Hc_" + entry);
			if(!entry.startsWith("Hc" + QString::number(no) + "_"))
				dest.rename(entry, "Hc" + QString::number(no) + "_" + entry);
		}
	};

	extract(tmpDir, outDirData, "xBitmap.lod");
	extract(tmpDir, outDirData, "xlBitmap.lod");
	extract(tmpDir, outDirSprites, "xSprite.lod");
	extract(tmpDir, outDirSprites, "xlSprite.lod");
	extract(tmpDir, outDirVideo, "xVideo.vid");
	extract(tmpDir, outDirSounds, "xSound.snd");

	tmpDir.cdUp();
	if(tmpDir.entryList({"maps"}, QDir::Filter::Dirs).size())
	{
		QDir tmpDirMaps = tmpDir.filePath(tmpDir.entryList({"maps"}, QDir::Filter::Dirs).front());
		for(const auto & entry : tmpDirMaps.entryList())
			QFile(tmpDirMaps.filePath(entry)).copy(outDirData.filePath(entry));
	}

	tmpDir.cdUp();
	QDir tmpDirData = tmpDir.filePath(tmpDir.entryList({"data"}, QDir::Filter::Dirs).front());
	extract(tmpDirData, outDirData, "bitmap.lod", std::vector<std::string>{"HPS137", "HPS138", "HPS139", "HPS140", "HPS141", "HPS142", "HPL137", "HPL138", "HPL139", "HPL140", "HPL141", "HPL142"});
	extract(tmpDirData, outDirData, "lbitmap.lod", std::vector<std::string>{"INTRORIM"});

	rename(outDirData);
	rename(outDirSprites);
	rename(outDirVideo);
	rename(outDirSounds);

	if(!outDirMaps.exists())
		outDirMaps.mkpath(".");
	QString campaignFileName = "Hc" + QString::number(no) + "_Main.h3c";
	QFile(outDirData.filePath(outDirData.entryList({campaignFileName}).front())).copy(outDirMaps.filePath(campaignFileName));
}

void ChroniclesExtractor::installChronicles(QStringList exe)
{
	extractionFile = -1;
	fileCount = exe.size();
	for(QString f : exe)
	{
		extractionFile++;
		QFile file(f);

		int chronicleNo = getChronicleNo(file);
		if(!chronicleNo)
			continue;

		if(!handleTempDir(true))
			continue;

		if(!extractGogInstaller(f))
			continue;
		
		createBaseMod();
		createChronicleMod(chronicleNo);

		handleTempDir(false);
	}
}
