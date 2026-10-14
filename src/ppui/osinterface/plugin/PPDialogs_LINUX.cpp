/*
 *  ppui/osinterface/plugin/PPDialogs_LINUX.cpp
 *
 *  The Linux plug-in's message box, file panels and save prompt: zenity's, else kdialog's, run as a child
 *  process. The SDL build's dialogs run an SDL event loop of their own, which a plug-in has none of. Waiting
 *  for the child blocks only the editor's thread, since Hollow's X11 window runs on a thread of its own.
 *  With neither tool installed a message goes to stderr and a panel or the prompt cancels.
 */

#include "PPMessageBox.h"
#include "PPOpenPanel.h"
#include "PPSavePanel.h"
#include "PPQuitSaveAlert.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>

static std::string quote(const std::string& s)
{
	std::string q = "'";
	for (char c : s)
		q += c == '\'' ? std::string("'\\''") : std::string(1, c);
	return q + "'";
}

static const char* tool()
{
	static const char* found = std::system("command -v zenity >/dev/null 2>&1") == 0 ? "zenity"
	                         : std::system("command -v kdialog >/dev/null 2>&1") == 0 ? "kdialog" : "";
	return found;
}

// Runs the command, returns its exit code and its first line of output
static int run(const std::string& cmd, std::string& out)
{
	out.clear();
	FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
	if (!p)
		return -1;
	char buf[4096];
	while (fgets(buf, sizeof buf, p))
		out += buf;
	int rc = pclose(p);
	size_t nl = out.find('\n');
	if (nl != std::string::npos)
		out.resize(nl);
	return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

static std::string patterns(PPSimpleVector<Descriptor>& items)
{
	std::string p;
	for (pp_int32 i = 0; i < items.size(); i++)
		p += (p.empty() ? "*." : " *.") + std::string(items.get(i)->extension.getStrBuffer());
	return p;
}

PPMessageBox::ReturnCodes PPMessageBox::runModal()
{
	std::string out, t = tool(), cap = caption.getStrBuffer(), text = content.getStrBuffer();
	if (t == "zenity")
		run("zenity --info --title=" + quote(cap) + " --text=" + quote(text), out);
	else if (t == "kdialog")
		run("kdialog --title " + quote(cap) + " --msgbox " + quote(text), out);
	else
		std::fprintf(stderr, "MilkyTracker: %s: %s\n", cap.c_str(), text.c_str());
	return ReturnCodeOK;
}

PPOpenPanel::PPOpenPanel(PPScreen* screen, const char* caption) :
	PPModalDialog(screen)
{
	this->caption = new char[strlen(caption)+1];
	strcpy(this->caption, caption);
}

PPOpenPanel::~PPOpenPanel()
{
	delete[] caption;
}

void PPOpenPanel::addExtension(const PPString& ext, const PPString& desc)
{
	items.add(new Descriptor(ext, desc));
}

static PPModalDialog::ReturnCodes filePanel(const char* caption, PPSimpleVector<Descriptor>& items, bool save, const PPSystemString& name, PPSystemString& fileName)
{
	std::string out, t = tool(), pat = patterns(items);
	int rc = -1;
	if (t == "zenity")
		rc = run("zenity --file-selection --title=" + quote(caption) + (save ? " --save --confirm-overwrite --filename=" + quote(name.getStrBuffer()) : std::string()) +
		         (pat.empty() ? std::string() : " --file-filter=" + quote(std::string(caption) + " | " + pat)) + " --file-filter=" + quote("All files | *"), out);
	else if (t == "kdialog")
		rc = run(std::string("kdialog --title ") + quote(caption) + (save ? " --getsavefilename " + quote(name.getStrBuffer()) : std::string(" --getopenfilename .")) +
		         " " + quote(pat.empty() ? "*" : pat + "|" + caption), out);
	if (rc != 0 || out.empty())
	{
		fileName = "";
		return PPModalDialog::ReturnCodeCANCEL;
	}
	fileName = out.c_str();
	return PPModalDialog::ReturnCodeOK;
}

PPOpenPanel::ReturnCodes PPOpenPanel::runModal()
{
	return filePanel(caption, items, false, "", fileName);
}

PPSavePanel::ReturnCodes PPSavePanel::runModal()
{
	return filePanel(caption, items, true, defaultFileName, fileName);
}

// Save, Don't Save or Cancel. Cancel when it cannot ask, so nothing is lost unasked.
PPQuitSaveAlert::ReturnCodes PPQuitSaveAlert::runModal()
{
	static const char* q = "Do you want to save the changes you made to your documents?";
	std::string out, t = tool();
	if (t == "zenity")
	{
		int rc = run(std::string("zenity --question --title='Save changes' --text=") + quote(q) +
		             " --ok-label=Save --cancel-label=Cancel --extra-button=\"Don't Save\"", out);
		return rc == 0 ? ReturnCodeOK : out == "Don't Save" ? ReturnCodeNO : ReturnCodeCANCEL;
	}
	if (t == "kdialog")
	{
		int rc = run(std::string("kdialog --title 'Save changes' --yesnocancel ") + quote(q), out);
		return rc == 0 ? ReturnCodeOK : rc == 1 ? ReturnCodeNO : ReturnCodeCANCEL;
	}
	return ReturnCodeCANCEL;
}
