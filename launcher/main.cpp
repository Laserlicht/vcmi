/*
 * main.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "main.h"
#include "mainwindow_moc.h"
#include "prepare.h"

#include "../lib/VCMIDirs.h"

#include <QApplication>
#include <QProcess>
#include <QMessageBox>
#include <QDir>

// Conan workaround https://github.com/conan-io/conan-center-index/issues/13332
#ifdef VCMI_IOS
#if __has_include("QIOSIntegrationPlugin.h")
#include "QIOSIntegrationPlugin.h"
#endif
int argcForClient;
char ** argvForClient;
#elif defined(VCMI_ANDROID)
#include <QAndroidJniObject>
#include <QtAndroid>
#endif

// android must export main explicitly to make it visible in the shared library
#ifdef VCMI_ANDROID
#define EXPORT ELF_VISIBILITY
#else
#define EXPORT
#endif

int EXPORT main(int argc, char * argv[])
{
	int result;
#ifdef VCMI_IOS
	{
#endif
	QApplication vcmilauncher(argc, argv);

	launcher::prepare();

	MainWindow mainWindow;
	mainWindow.show();

	result = vcmilauncher.exec();
#ifdef VCMI_IOS
	}
	if (result == 0)
		launchGame(argcForClient, argvForClient);
#endif
	return result;
}

void startGame(const QStringList & args)
{
	logGlobal->warn("Starting game with the arguments: %s", args.join(" ").toStdString());

#ifdef VCMI_IOS
	static const char clientName[] = "vcmiclient";
	argcForClient = args.size() + 1; //first argument is omitted
	argvForClient = new char*[argcForClient];
	argvForClient[0] = new char[strlen(clientName)+1];
	strcpy(argvForClient[0], clientName);
	for(int i = 1; i < argcForClient; ++i)
	{
		std::string s = args.at(i - 1).toStdString();
		argvForClient[i] = new char[s.size() + 1];
		strcpy(argvForClient[i], s.c_str());
	}
	qApp->quit();
#elif defined(VCMI_ANDROID)
	QtAndroid::androidActivity().callMethod<void>("onLaunchGameBtnPressed");
#else
	startExecutable(pathToQString(VCMIDirs::get().clientPath()), args);
#endif
}

void startEditor(const QStringList & args)
{
#ifdef ENABLE_EDITOR
	startExecutable(pathToQString(VCMIDirs::get().mapEditorPath()), args);
#endif
}

#ifndef VCMI_MOBILE
void startExecutable(QString name, const QStringList & args)
{
	QProcess process;
	
	// Start the executable
	if(process.startDetached(name, args))
	{
		qApp->quit();
	}
	else
	{
		QMessageBox::critical(qApp->activeWindow(),
							  "Error starting executable",
							  "Failed to start " + name + "\n"
							  "Reason: " + process.errorString(),
							  QMessageBox::Ok,
							  QMessageBox::Ok);
	}
}
#endif
