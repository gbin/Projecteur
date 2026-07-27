// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "aboutdlg.h"

#include "projecteur-GitVersion.h"

#include <algorithm>
#include <random>
#include <vector>

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>

namespace {
  // -----------------------------------------------------------------------------------------------
  /// Contributor (name, github_name, email, url)
  struct Contributor
  {
    explicit Contributor(const QString& name = {}, const QString& github_name = {},
                         const QString& email = {}, const QString& url = {})
      : name(name), github_name(github_name), email(email), url(url) {}

    QString toHtml() const
    {
      auto html = QString("<b>%1</b>").arg(name.isEmpty()
        ? QString("<a href=\"https://github.com/%1\">%1</a>").arg(github_name)
        : name);

      if (email.size()) {
        html += QString(" &lt;%1&gt;").arg(email);
      }

      if (url.size()) {
        html += QString(" <a href=\"%1\">%1</a>").arg(url);
      }
      else if (!name.isEmpty()) {
        html += QString(" - <i>github:</i> <a href=\"https://github.com/%1\">%1</a>").arg(github_name);
      }
      return html;
    }

    QString name;
    QString github_name;
    QString email;
    QString url;
  };

  // -----------------------------------------------------------------------------------------------
  QString getContributorsHtml()
  {
    static std::vector<Contributor> contributors =
    {
      Contributor("Ricardo Jesus", "rj-jesus"),
      Contributor("Mayank Suman", "mayanksuman"),
      Contributor("Tiziano Müller", "dev-zero"),
      Contributor("Torsten Maehne", "maehne"),
      Contributor("TBK", "TBK"),
      Contributor("Louie Lu", "mlouielu"),
      Contributor("fmuelle4711", "fmuelle4711"),
      Contributor("Deniz Bahadir", "Bagira80"),
      Contributor("Tomáš Chvátal", "scarabeusiv"),
      Contributor("Brandon Johnson", "dbrandonjohnson"),
      Contributor("Stuart Prescott", "llimeht"),
      Contributor("Crista Renouard", "Lumnicence"),
      Contributor("freddii", "freddii"),
      Contributor("Matthias Blümel", "Blaimi"),
      Contributor("Grzegorz Szymaszek", "gszy"),
      Contributor("TheAssassin", "TheAssassin"),
    };

    static std::mt19937 g(std::random_device{}());
    std::shuffle(contributors.begin(), contributors.end(), g);

    QStringList contributorsHtml;
    for (const auto& contributor : contributors) {
      contributorsHtml.append(contributor.toHtml());
    }
    return contributorsHtml.join("<br>");
  }
} // end anonymous namespace

// -------------------------------------------------------------------------------------------------
AboutDialog::AboutDialog(QWidget* parent)
  : QDialog(parent)
  , m_tabWidget(new QTabWidget(this))
{
  setWindowTitle(tr("About %1", "%1=application name").arg(QCoreApplication::applicationName()));
  setWindowIcon(QIcon(":/icons/projecteur-tray.svg"));

  const auto hbox = new QHBoxLayout();
  const auto iconLabel = new QLabel(this);
  iconLabel->setPixmap(QIcon(":/icons/projecteur-tray.svg").pixmap(QSize(128,128)));
  hbox->addWidget(iconLabel);

  hbox->addWidget(m_tabWidget, 1);

  m_tabWidget->addTab(createVersionInfoWidget(), tr("Version"));
  m_tabWidget->addTab(createContributorInfoWidget(), tr("Contributors"));
  m_tabWidget->addTab(createThirdPartyLicensesWidget(), tr("Licenses"));

  const auto bbox = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  connect(bbox, &QDialogButtonBox::clicked, this, &QDialog::accept);

  const auto mainVbox = new QVBoxLayout(this);
  mainVbox->addLayout(hbox);
  mainVbox->addSpacing(10);
  mainVbox->addWidget(bbox);
}

// -------------------------------------------------------------------------------------------------
void AboutDialog::showEvent(QShowEvent* e)
{
  QDialog::showEvent(e);
  m_tabWidget->setCurrentIndex(0);
}

// -------------------------------------------------------------------------------------------------
QWidget* AboutDialog::createVersionInfoWidget()
{
  const auto versionInfoWidget = new QWidget(this);
  const auto vbox = new QVBoxLayout(versionInfoWidget);
  const auto versionLabel = new QLabel(QString("<b>%1</b><br>%2")
                                 .arg(QCoreApplication::applicationName(),
                                      tr("Version %1", "%1=application version number")
                                      .arg(projecteur::version_string())), this);
  vbox->addWidget(versionLabel);
  const auto vInfo = QString("<i>git-branch:</i> %1<br><i>git-hash:</i> %2<br><i>build-type:</i> %3")
                              .arg(projecteur::version_branch(),
                                   projecteur::version_shorthash(),
                                   projecteur::version_buildtype());
  versionLabel->setToolTip(vInfo);

  if (QString(projecteur::version_flag()).size() ||
       (QString(projecteur::version_branch()) != "master"
        && QString(projecteur::version_branch()) != "not-within-git-repo"))
  {
    vbox->addSpacing(4);
    vbox->addWidget(new QLabel(vInfo, this));
  }

  vbox->addSpacing(4);
  const auto projectLinksLabel = new QLabel(
    tr("Independent, unofficial KDE/Wayland fork maintained by "
       "<a href=\"https://github.com/gbin\">Guillaume Binet</a>.<br>"
       "Fork source and support: "
       "<a href=\"https://github.com/gbin/Projecteur-kde\">gbin/Projecteur-kde</a><br>"
       "Based on <a href=\"https://github.com/jahnf/Projecteur\">Projecteur</a> "
       "by Jahn Fuchs."), this);
  projectLinksLabel->setWordWrap(true);
  projectLinksLabel->setTextFormat(Qt::TextFormat::RichText);
  projectLinksLabel->setOpenExternalLinks(true);
  vbox->addWidget(projectLinksLabel);
  vbox->addSpacing(8);

  auto qtVerText = tr("Qt Version: %1", "%1=qt version number").arg(QT_VERSION_STR);
  if (QString(QT_VERSION_STR) != qVersion()) {
    qtVerText += QString(" (runtime: %1)").arg(qVersion());
  }
  vbox->addWidget(new QLabel(qtVerText, this));
  vbox->addSpacing(15);
  vbox->addWidget(new QLabel("Copyright 2018-2021 Jahn Fuchs", this));
  vbox->addWidget(new QLabel("Fork modifications Copyright 2026 Guillaume Binet", this));
  auto licenseText = new QLabel(tr("This project is distributed under the <br>"
                                   "<a href=\"https://github.com/gbin/Projecteur-kde/blob/develop/LICENSE.md\">"
                                   "MIT License</a>"), this);
  licenseText->setWordWrap(true);
  licenseText->setTextFormat(Qt::TextFormat::RichText);
  licenseText->setOpenExternalLinks(true);
  vbox->addWidget(licenseText);

  vbox->addStretch(1);
  return versionInfoWidget;
}

// -------------------------------------------------------------------------------------------------
QWidget* AboutDialog::createContributorInfoWidget()
{
  const auto contributorWidget = new QWidget(this);
  const auto vbox = new QVBoxLayout(contributorWidget);

  const auto label = new QLabel(tr("Contributors, in random order:"), contributorWidget);
  vbox->addWidget(label);

  const auto textBrowser = new QTextBrowser(contributorWidget);
  textBrowser->setWordWrapMode(QTextOption::NoWrap);
  textBrowser->setOpenLinks(true);
  textBrowser->setOpenExternalLinks(true);
  textBrowser->setFont([textBrowser]()
  {
    auto font = textBrowser->font();
    font.setPointSizeF(font.pointSizeF() - 2.0);
    return font;
  }());

  // randomize contributors list on every contributors tab selection
  connect(m_tabWidget, &QTabWidget::currentChanged, this,
  [contributorWidget, textBrowser, this](int){
    if (contributorWidget == m_tabWidget->currentWidget()) {
      textBrowser->setHtml(getContributorsHtml());
    }
  });

  vbox->addWidget(textBrowser);
  return contributorWidget;
}

// -------------------------------------------------------------------------------------------------
QWidget* AboutDialog::createThirdPartyLicensesWidget()
{
  const auto tpLicenceWidget = new QWidget(this);
  const auto layout = new QVBoxLayout(tpLicenceWidget);

  struct ThirdPartyProject {
    const QString projectName;
    const QString projectUrl;
    const QString copyrightNotice;
    const QString licenseName;
    const QString licenseUrl;
  };

  static const std::vector<ThirdPartyProject> thirdPartyProjects = {
    ThirdPartyProject{ "Qt Toolkit", "https://www.qt.io", "Copyright (C) The Qt Company Ltd.", "GPL/LGPLv3", "" },
  };

  const auto textBrowser = new QTextBrowser(tpLicenceWidget);
  layout->addWidget(textBrowser);

  textBrowser->setOpenLinks(true);
  textBrowser->setOpenExternalLinks(true);
  textBrowser->setWordWrapMode(QTextOption::NoWrap);
  textBrowser->setFont([textBrowser]()
  {
    auto font = textBrowser->font();
    font.setPointSizeF(font.pointSizeF() - 2.5);
    return font;
  }());

  QString html = "<style> li { margin-left: 10px; } ul { -qt-list-indent: 0 } </style>";

  html += "<ul>";
  for (const auto& tpl : thirdPartyProjects)
  {
    html += "<li>";
    if (tpl.projectUrl.size()) {
      html += QString("<b><a href=\"%1\">%2</a></b>").arg(tpl.projectUrl, tpl.projectName);
    } else {
      html += QString("<b>%1</b>").arg(tpl.projectName);
    }

    if (tpl.copyrightNotice.size()) {
      html += "<br/><tt>" + tpl.copyrightNotice + "</tt>";
    }

    if (tpl.licenseUrl.size()) {
      html += QString("<br/><a href=\"%1\">%2</a>").arg(tpl.licenseUrl, tpl.licenseName);
    } else {
      html += QString("<br/><i>License</i>: %1").arg(tpl.licenseName);
    }

    html += "</li>";
  }
  html += "</ul>";

  textBrowser->setHtml(html);
  return tpLicenceWidget;
}
