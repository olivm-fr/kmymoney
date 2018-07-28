/*
 * Copyright 2002-2004  Kevin Tambascio <ktambascio@users.sourceforge.net>
 * Copyright 2002-2016  Thomas Baumgart <tbaumgart@kde.org>
 * Copyright 2004-2005  Ace Jones <acejones@users.sourceforge.net>
 * Copyright 2006       Darren Gould <darren_gould@gmx.de>
 * Copyright 2017-2018  Łukasz Wojniłowicz <lukasz.wojnilowicz@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mymoneystoragexml.h"

// ----------------------------------------------------------------------------
// QT Includes

#include <QMap>
#include <QXmlLocator>
#include <QTextStream>
#include <QList>
#include <QDomDocument>
#include <QDomElement>
#include <QDebug>
#include <QDate>
#include <QColor>

// ----------------------------------------------------------------------------
// KDE Includes

#include <KLocalizedString>

// ----------------------------------------------------------------------------
// Project Includes

#include "mymoneystoragemgr.h"
#include "mymoneyexception.h"
#include "mymoneykeyvaluecontainer.h"
#include "mymoneyaccount.h"
#include "mymoneysecurity.h"
#include "mymoneymoney.h"
#include "mymoneyschedule.h"
#include "mymoneypayee.h"
#include "mymoneytag.h"
#include "mymoneyreport.h"
#include "mymoneybudget.h"
#include "mymoneyinstitution.h"
#include "mymoneystoragenames.h"
#include "mymoneyutils.h"
#include "mymoneyprice.h"
#include "mymoneycostcenter.h"
#include "mymoneytransaction.h"
#include "mymoneysplit.h"
#include "onlinejob.h"
#include "onlinejobadministration.h"
#include "plugins/xmlhelper/xmlstoragehelper.h"
#include "mymoneyenums.h"

using namespace eMyMoney;

unsigned int MyMoneyStorageXML::fileVersionRead = 0;
unsigned int MyMoneyStorageXML::fileVersionWrite = 0;


class MyMoneyStorageXML::Private
{
  friend class MyMoneyStorageXML;
public:
  Private() : m_nextTransactionID(0) {}

  QMap<QString, MyMoneyInstitution> iList;
  QMap<QString, MyMoneyAccount> aList;
  QMap<QString, MyMoneyTransaction> tList;
  QMap<QString, MyMoneyPayee> pList;
  QMap<QString, MyMoneyTag> taList;
  QMap<QString, MyMoneySchedule> sList;
  QMap<QString, MyMoneySecurity> secList;
  QMap<QString, MyMoneyReport> rList;
  QMap<QString, MyMoneyBudget> bList;
  QMap<QString, onlineJob> onlineJobList;
  QMap<MyMoneySecurityPair, MyMoneyPriceEntries> prList;
  QMap<QString, MyMoneyCostCenter> ccList;

  QString           m_fromSecurity;
  QString           m_toSecurity;
  unsigned long     m_nextTransactionID;
  static const int  TRANSACTION_ID_SIZE = 18;

  QString nextTransactionID() {
    QString id;
    id.setNum(++m_nextTransactionID);
    id = 'T' + id.rightJustified(TRANSACTION_ID_SIZE, '0');
    return id;
  }
};

namespace test { bool readRCFfromXMLDoc(QList<MyMoneyReport>& list, QDomDocument* doc); }
namespace test { void writeRCFtoXMLDoc(const MyMoneyReport& filter, QDomDocument* doc); }

class MyMoneyXmlContentHandler : public QXmlContentHandler
{
  friend class MyMoneyXmlContentHandlerTest;
  friend class MyMoneyStorageXML;
  friend bool test::readRCFfromXMLDoc(QList<MyMoneyReport>& list, QDomDocument* doc);
  friend void test::writeRCFtoXMLDoc(const MyMoneyReport& filter, QDomDocument* doc);

public:
  MyMoneyXmlContentHandler(MyMoneyStorageXML* reader);
  virtual ~MyMoneyXmlContentHandler() {}
  virtual void setDocumentLocator(QXmlLocator * locator) final override {
    m_loc = locator;
  }
  virtual bool startDocument() final override;
  virtual bool endDocument() final override;
  virtual bool startPrefixMapping(const QString & prefix, const QString & uri) final override;
  virtual bool endPrefixMapping(const QString & prefix) final override;
  virtual bool startElement(const QString & namespaceURI, const QString & localName, const QString & qName, const QXmlAttributes & atts) final override;
  virtual bool endElement(const QString & namespaceURI, const QString & localName, const QString & qName) final override;
  virtual bool characters(const QString & ch) final override;
  virtual bool ignorableWhitespace(const QString & ch) final override;
  virtual bool processingInstruction(const QString & target, const QString & data) final override;
  virtual bool skippedEntity(const QString & name) final override;
  virtual QString errorString() const final override;
private:
  MyMoneyStorageXML* m_reader;
  QXmlLocator*       m_loc;
  int                m_level;
  int                m_elementCount;
  QDomDocument       m_doc;

  /**
   * @node Text in the xml file is not added to this QDomElement. Only tags and their attributes are added.
   */
  QDomElement        m_baseNode;

  /**
   * @node Text in the xml file is not added to this QDomElement. Only tags and their attributes are added.
   */
  QDomElement        m_currNode;
  QString            m_errMsg;

  static void writeBaseXML(const QString &id, QDomDocument &document, QDomElement &el);
  static void addToKeyValueContainer(MyMoneyKeyValueContainer &container, const QDomElement &node);
  static void writeKeyValueContainer(const MyMoneyKeyValueContainer &container, QDomDocument &document, QDomElement &parent);
  static MyMoneyTransaction readTransaction(const QDomElement &node, bool assignEntryDateIfEmpty = true);
  static void writeTransaction(const MyMoneyTransaction &transaction, QDomDocument &document, QDomElement &parent);
  static MyMoneySplit readSplit(const QDomElement &node);
  static void writeSplit(const MyMoneySplit &_split, QDomDocument &document, QDomElement &parent);
  static MyMoneyAccount readAccount(const QDomElement &node);
  static void writeAccount(const MyMoneyAccount &account, QDomDocument &document, QDomElement &parent);
  static MyMoneyPayee readPayee(const QDomElement &node);
  static void writePayee(const MyMoneyPayee &payee, QDomDocument &document, QDomElement &parent);
  static MyMoneyTag readTag(const QDomElement &node);
  static void writeTag(const MyMoneyTag &tag, QDomDocument &document, QDomElement &parent);
  static MyMoneySecurity readSecurity(const QDomElement &node);
  static void writeSecurity(const MyMoneySecurity &security, QDomDocument &document, QDomElement &parent);
  static MyMoneyInstitution readInstitution(const QDomElement &node);
  static void writeInstitution(const MyMoneyInstitution &institution, QDomDocument &document, QDomElement &parent);
  static MyMoneySchedule readSchedule(const QDomElement &node);
  static void writeSchedule(const MyMoneySchedule &schedule, QDomDocument &document, QDomElement &parent);
  static onlineJob readOnlineJob(const QDomElement &node);
  static void writeOnlineJob(const onlineJob &job, QDomDocument &document, QDomElement &parent);
  static MyMoneyCostCenter readCostCenter(const QDomElement &node);
  static void writeCostCenter(const MyMoneyCostCenter &costCenter, QDomDocument &document, QDomElement &parent);
};

MyMoneyXmlContentHandler::MyMoneyXmlContentHandler(MyMoneyStorageXML* reader) :
    m_reader(reader),
    m_loc(0),
    m_level(0),
    m_elementCount(0)
{
}

bool MyMoneyXmlContentHandler::startDocument()
{
  qDebug("startDocument");
  return true;
}

bool MyMoneyXmlContentHandler::endDocument()
{
  qDebug("endDocument");
  return true;
}

bool MyMoneyXmlContentHandler::skippedEntity(const QString & /* name */)
{
  // qDebug(QString("Skipped entity '%1'").arg(name));
  return true;
}

bool MyMoneyXmlContentHandler::startPrefixMapping(const QString& /*prefix */, const QString & /* uri */)
{
  // qDebug(QString("start prefix '%1', '%2'").arg(prefix).arg(uri));
  return true;
}

bool MyMoneyXmlContentHandler::endPrefixMapping(const QString& /* prefix */)
{
  // qDebug(QString("end prefix '%1'").arg(prefix));
  return true;
}

bool MyMoneyXmlContentHandler::startElement(const QString& /* namespaceURI */, const QString& /* localName */, const QString& qName, const QXmlAttributes & atts)
{
  if (m_level == 0) {
    QString s = qName.toUpper();
    if (s == nodeName(Node::Transaction)
        || s == nodeName(Node::Account)
        || s == nodeName(Node::Price)
        || s == nodeName(Node::Payee)
        || s == nodeName(Node::Tag)
        || s == nodeName(Node::CostCenter)
        || s == nodeName(Node::Currency)
        || s == nodeName(Node::Security)
        || s == nodeName(Node::KeyValuePairs)
        || s == nodeName(Node::Institution)
        || s == nodeName(Node::Report)
        || s == nodeName(Node::Budget)
        || s == tagName(Tag::FileInfo)
        || s == tagName(Tag::User)
        || s == nodeName(Node::ScheduleTX)
        || s == nodeName(Node::OnlineJob)) {
      m_baseNode = m_doc.createElement(qName);
      for (int i = 0; i < atts.count(); ++i) {
        m_baseNode.setAttribute(atts.qName(i), atts.value(i));
      }
      m_currNode = m_baseNode;
      m_level = 1;

    } else if (s == tagName(Tag::Transactions)) {
      if (atts.count()) {
        int count = atts.value(attributeName(Attribute::General::Count)).toInt();
        m_reader->signalProgress(0, count, i18n("Loading transactions..."));
        m_elementCount = 0;
      }
    } else if (s == tagName(Tag::Accounts)) {
      if (atts.count()) {
        int count = atts.value(attributeName(Attribute::General::Count)).toInt();
        m_reader->signalProgress(0, count, i18n("Loading accounts..."));
        m_elementCount = 0;
      }
    } else if (s == tagName(Tag::Securities)) {
      qDebug("reading securities");
      if (atts.count()) {
        int count = atts.value(attributeName(Attribute::General::Count)).toInt();
        m_reader->signalProgress(0, count, i18n("Loading securities..."));
        m_elementCount = 0;
      }
    } else if (s == tagName(Tag::Currencies)) {
      if (atts.count()) {
        int count = atts.value(attributeName(Attribute::General::Count)).toInt();
        m_reader->signalProgress(0, count, i18n("Loading currencies..."));
        m_elementCount = 0;
      }
    } else if (s == tagName(Tag::Reports)) {
      if (atts.count()) {
        int count = atts.value(attributeName(Attribute::General::Count)).toInt();
        m_reader->signalProgress(0, count, i18n("Loading reports..."));
        m_elementCount = 0;
      }
    } else if (s == tagName(Tag::Prices)) {
      if (atts.count()) {
        int count = atts.value(attributeName(Attribute::General::Count)).toInt();
        m_reader->signalProgress(0, count, i18n("Loading prices..."));
        m_elementCount = 0;
      }
    } else if (s == nodeName(Node::PricePair)) {
      if (atts.count()) {
        m_reader->d->m_fromSecurity = atts.value(attributeName(Attribute::General::From));
        m_reader->d->m_toSecurity = atts.value(attributeName(Attribute::General::To));
      }
    } else if (s == tagName(Tag::CostCenters)) {
      if(atts.count()) {
        int count = atts.value(attributeName(Attribute::General::Count)).toInt();
        m_reader->signalProgress(0, count, i18n("Loading cost center..."));
        m_elementCount = 0;
      }
    }

  } else {
    m_level++;
    QDomElement node = m_doc.createElement(qName);
    for (int i = 0; i < atts.count(); ++i) {
      node.setAttribute(atts.qName(i), atts.value(i));
    }
    m_currNode.appendChild(node);
    m_currNode = node;
  }
  return true;
}


bool MyMoneyXmlContentHandler::endElement(const QString& /* namespaceURI */, const QString& /* localName */, const QString& qName)
{
  bool rc = true;
  QString s = qName.toUpper();
  if (m_level) {
    m_currNode = m_currNode.parentNode().toElement();
    m_level--;
    if (!m_level) {
      try {
        if (s == nodeName(Node::Transaction)) {
          auto t0 = readTransaction(m_baseNode);
          if (!t0.id().isEmpty()) {
            MyMoneyTransaction t1(m_reader->d->nextTransactionID(), t0);
            m_reader->d->tList[t1.uniqueSortKey()] = t1;
          }
          m_reader->signalProgress(++m_elementCount, 0);
        } else if (s == nodeName(Node::Account)) {
          auto a = readAccount(m_baseNode);
          if (!a.id().isEmpty())
            m_reader->d->aList[a.id()] = a;
          m_reader->signalProgress(++m_elementCount, 0);
        } else if (s == nodeName(Node::Payee)) {
          auto p = readPayee(m_baseNode);
          if (!p.id().isEmpty())
            m_reader->d->pList[p.id()] = p;
          m_reader->signalProgress(++m_elementCount, 0);
        } else if (s == nodeName(Node::Tag)) {
          auto ta = readTag(m_baseNode);
          if (!ta.id().isEmpty())
            m_reader->d->taList[ta.id()] = ta;
          m_reader->signalProgress(++m_elementCount, 0);
        } else if (s == nodeName(Node::Currency)) {
          auto sec = readSecurity(m_baseNode);
          if (!sec.id().isEmpty())
            m_reader->d->secList[sec.id()] = sec;
          m_reader->signalProgress(++m_elementCount, 0);
        } else if (s == nodeName(Node::Security)) {
          auto sec = readSecurity(m_baseNode);
          if (!sec.id().isEmpty())
            m_reader->d->secList[sec.id()] = sec;
          m_reader->signalProgress(++m_elementCount, 0);
        } else if (s == nodeName(Node::KeyValuePairs)) {
          addToKeyValueContainer(*m_reader->m_storage, m_baseNode);
        } else if (s == nodeName(Node::Institution)) {
          auto i = readInstitution(m_baseNode);
          if (!i.id().isEmpty())
            m_reader->d->iList[i.id()] = i;
        } else if (s == nodeName(Node::Report)) {
          auto r = MyMoneyXmlContentHandler2::readReport(m_baseNode);
          if (!r.id().isEmpty())
            m_reader->d->rList[r.id()] = r;
          m_reader->signalProgress(++m_elementCount, 0);
        } else if (s == nodeName(Node::Budget)) {
          auto b = MyMoneyXmlContentHandler2::readBudget(m_baseNode);
          if (!b.id().isEmpty())
            m_reader->d->bList[b.id()] = b;
        } else if (s == tagName(Tag::FileInfo)) {
          rc = m_reader->readFileInformation(m_baseNode);
          m_reader->signalProgress(-1, -1);
        } else if (s == tagName(Tag::User)) {
          rc = m_reader->readUserInformation(m_baseNode);
          m_reader->signalProgress(-1, -1);
        } else if (s == nodeName(Node::ScheduleTX)) {
          auto sch = readSchedule(m_baseNode);
          if (!sch.id().isEmpty())
            m_reader->d->sList[sch.id()] = sch;
        } else if (s == nodeName(Node::Price)) {
          MyMoneyPrice p(m_reader->d->m_fromSecurity, m_reader->d->m_toSecurity, m_baseNode);
          m_reader->d->prList[MyMoneySecurityPair(m_reader->d->m_fromSecurity, m_reader->d->m_toSecurity)][p.date()] = p;
          m_reader->signalProgress(++m_elementCount, 0);
        } else if (s == nodeName(Node::OnlineJob)) {
          auto job = readOnlineJob(m_baseNode);
          if (!job.id().isEmpty())
            m_reader->d->onlineJobList[job.id()] = job;
        } else if (s == nodeName(Node::CostCenter)) {
          auto c = readCostCenter(m_baseNode);
          if(!c.id().isEmpty()) {
            m_reader->d->ccList[c.id()] = c;
          }
          m_reader->signalProgress(++m_elementCount, 0);
        } else {
          m_errMsg = i18n("Unknown XML tag %1 found in line %2", qName, m_loc->lineNumber());
          qWarning() << m_errMsg;
          rc = false;
        }
      } catch (const MyMoneyException &e) {
        m_errMsg = i18n("Exception while creating a %1 element: %2", s, e.what());
        qWarning() << m_errMsg;
        rc = false;
      }
      m_doc = QDomDocument();
    }
  } else {
    if (s == tagName(Tag::Institutions)) {
      // last institution read, now dump them into the engine
      m_reader->m_storage->loadInstitutions(m_reader->d->iList);
      m_reader->d->iList.clear();
    } else if (s == tagName(Tag::Accounts)) {
      // last account read, now dump them into the engine
      m_reader->m_storage->loadAccounts(m_reader->d->aList);
      m_reader->d->aList.clear();
      m_reader->signalProgress(-1, -1);
    } else if (s == tagName(Tag::Payees)) {
      // last payee read, now dump them into the engine
      m_reader->m_storage->loadPayees(m_reader->d->pList);
      m_reader->d->pList.clear();
    } else if (s == tagName(Tag::Tags)) {
      // last tag read, now dump them into the engine
      m_reader->m_storage->loadTags(m_reader->d->taList);
      m_reader->d->taList.clear();
    } else if (s == tagName(Tag::Transactions)) {
      // last transaction read, now dump them into the engine
      m_reader->m_storage->loadTransactions(m_reader->d->tList);
      m_reader->d->tList.clear();
      m_reader->signalProgress(-1, -1);
    } else if (s == tagName(Tag::Schedules)) {
      // last schedule read, now dump them into the engine
      m_reader->m_storage->loadSchedules(m_reader->d->sList);
      m_reader->d->sList.clear();
    } else if (s == tagName(Tag::Securities)) {
      // last security read, now dump them into the engine
      m_reader->m_storage->loadSecurities(m_reader->d->secList);
      m_reader->d->secList.clear();
      m_reader->signalProgress(-1, -1);
    } else if (s == tagName(Tag::Currencies)) {
      // last currency read, now dump them into the engine
      m_reader->m_storage->loadCurrencies(m_reader->d->secList);
      m_reader->d->secList.clear();
      m_reader->signalProgress(-1, -1);
    } else if (s == tagName(Tag::Reports)) {
      // last report read, now dump them into the engine
      m_reader->m_storage->loadReports(m_reader->d->rList);
      m_reader->d->rList.clear();
      m_reader->signalProgress(-1, -1);
    } else if (s == tagName(Tag::Budgets)) {
      // last budget read, now dump them into the engine
      m_reader->m_storage->loadBudgets(m_reader->d->bList);
      m_reader->d->bList.clear();
    } else if (s == tagName(Tag::Prices)) {
      // last price read, now dump them into the engine
      m_reader->m_storage->loadPrices(m_reader->d->prList);
      m_reader->d->bList.clear();
      m_reader->signalProgress(-1, -1);
    } else if (s == tagName(Tag::OnlineJobs)) {
      m_reader->m_storage->loadOnlineJobs(m_reader->d->onlineJobList);
      m_reader->d->onlineJobList.clear();
    } else if (s == tagName(Tag::CostCenters)) {
      m_reader->m_storage->loadCostCenters(m_reader->d->ccList);
      m_reader->d->ccList.clear();
      m_reader->signalProgress(-1, -1);
    }
  }
  return rc;
}

bool MyMoneyXmlContentHandler::characters(const QString& /* ch */)
{
  return true;
}

bool MyMoneyXmlContentHandler::ignorableWhitespace(const QString& /* ch */)
{
  return true;
}

bool MyMoneyXmlContentHandler::processingInstruction(const QString& /* target */, const QString& /* data */)
{
  return true;
}

QString MyMoneyXmlContentHandler::errorString() const
{
  return m_errMsg;
}

void MyMoneyXmlContentHandler::writeBaseXML(const QString &id, QDomDocument &document, QDomElement &el)
{
  Q_UNUSED(document);

  el.setAttribute(QStringLiteral("id"), id);
}

void MyMoneyXmlContentHandler::addToKeyValueContainer(MyMoneyKeyValueContainer &container, const QDomElement &node)
{
  if (!node.isNull()) {
    if (nodeName(Node::KeyValuePairs) != node.tagName())
      throw MYMONEYEXCEPTION_CSTRING("Node was not KEYVALUEPAIRS");
//    container.clear();

    QDomNodeList nodeList = node.elementsByTagName(elementName(Element::KVP::Pair));
    for (int i = 0; i < nodeList.count(); ++i) {
      const auto& el(nodeList.item(i).toElement());
      container.setValue(el.attribute(attributeName(Attribute::KVP::Key)),
                         el.attribute(attributeName(Attribute::KVP::Value)));
    }
  }
}

void MyMoneyXmlContentHandler::writeKeyValueContainer(const MyMoneyKeyValueContainer &container, QDomDocument &document, QDomElement &parent)
{
  const auto pairs = container.pairs();
  if (!pairs.isEmpty()) {
    auto el = document.createElement(nodeName(Node::KeyValuePairs));

    for (auto it = pairs.cbegin(); it != pairs.cend(); ++it) {
      auto pairElement = document.createElement(elementName(Element::KVP::Pair));
      pairElement.setAttribute(attributeName(Attribute::KVP::Key), it.key());
      pairElement.setAttribute(attributeName(Attribute::KVP::Value), it.value());
      el.appendChild(pairElement);
    }

    parent.appendChild(el);
  }
}

MyMoneyTransaction MyMoneyXmlContentHandler::readTransaction(const QDomElement &node, bool assignEntryDateIfEmpty)
{
  if (nodeName(Node::Transaction) != node.tagName())
    throw MYMONEYEXCEPTION_CSTRING("Node was not TRANSACTION");

  MyMoneyTransaction transaction(node.attribute(attributeName(Attribute::Account::ID)));

  //  d->m_nextSplitID = 1;

  transaction.setPostDate(QDate::fromString(node.attribute(attributeName(Attribute::Transaction::PostDate)), Qt::ISODate));
  auto entryDate = QDate::fromString(node.attribute(attributeName(Attribute::Transaction::EntryDate)),Qt::ISODate);
  if (!entryDate.isValid() && assignEntryDateIfEmpty)
    entryDate = QDate::currentDate();
  transaction.setEntryDate(entryDate);
  transaction.setBankID(node.attribute(attributeName(Attribute::Transaction::BankID)));
  transaction.setMemo(node.attribute(attributeName(Attribute::Transaction::Memo)));
  transaction.setCommodity(node.attribute(attributeName(Attribute::Transaction::Commodity)));

  QDomNode child = node.firstChild();
  auto transactionID = transaction.id();
  while (!child.isNull() && child.isElement()) {
    QDomElement c = child.toElement();
    if (c.tagName() == elementName(Element::Transaction::Splits)) {

      // Process any split information found inside the transaction entry.
      QDomNodeList nodeList = c.elementsByTagName(elementName(Element::Transaction::Split));
      for (auto i = 0; i < nodeList.count(); ++i) {
        auto s = readSplit(nodeList.item(i).toElement());

        if (!transaction.bankID().isEmpty())
          s.setBankID(transaction.bankID());
        if (!s.accountId().isEmpty())
          transaction.addSplit(s);
        else
          qDebug("Dropped split because it did not have an account id");

        s.setTransactionId(transactionID);
      }

    } else if (c.tagName() == nodeName(Node::KeyValuePairs)) {
      addToKeyValueContainer(transaction, c.toElement());
    }

    child = child.nextSibling();
  }
  transaction.setBankID(QString());

  return transaction;
}

void MyMoneyXmlContentHandler::writeTransaction(const MyMoneyTransaction &transaction, QDomDocument &document, QDomElement &parent)
{
  auto el = document.createElement(nodeName(Node::Transaction));

  writeBaseXML(transaction.id(), document, el);
  el.setAttribute(attributeName(Attribute::Transaction::PostDate), transaction.postDate().toString(Qt::ISODate));
  el.setAttribute(attributeName(Attribute::Transaction::Memo), transaction.memo());
  el.setAttribute(attributeName(Attribute::Transaction::EntryDate), transaction.entryDate().toString(Qt::ISODate));
  el.setAttribute(attributeName(Attribute::Transaction::Commodity), transaction.commodity());

  auto splitsElement = document.createElement(elementName(Element::Transaction::Splits));

  for (const auto &split : transaction.splits())
    writeSplit(split, document, splitsElement);
  el.appendChild(splitsElement);

  writeKeyValueContainer(transaction, document, el);

  parent.appendChild(el);
}

MyMoneySplit MyMoneyXmlContentHandler::readSplit(const QDomElement &node)
{
  if (nodeName(Node::Split) != node.tagName())
    throw MYMONEYEXCEPTION_CSTRING("Node was not SPLIT");

  MyMoneySplit split/*(node.attribute(attributeName(Attribute::Split::ID)))*/;

  addToKeyValueContainer(split, node.elementsByTagName(nodeName(Node::KeyValuePairs)).item(0).toElement());

  split.setPayeeId(node.attribute(attributeName(Attribute::Split::Payee)));

  QList<QString> tagList;
  QDomNodeList nodeList = node.elementsByTagName(elementName(Element::Split::Tag));
  for (auto i = 0; i < nodeList.count(); ++i)
    tagList << nodeList.item(i).toElement().attribute(attributeName(Attribute::Split::ID));
  split.setTagIdList(tagList);

  split.setReconcileDate(QDate::fromString(node.attribute(attributeName(Attribute::Split::ReconcileDate)), Qt::ISODate));
  split.setAction(node.attribute(attributeName(Attribute::Split::Action)));
  split.setReconcileFlag(static_cast<eMyMoney::Split::State>(node.attribute(attributeName(Attribute::Split::ReconcileFlag)).toInt()));
  split.setMemo(node.attribute(attributeName(Attribute::Split::Memo)));
  split.setValue(MyMoneyMoney(node.attribute(attributeName(Attribute::Split::Value))));
  split.setShares(MyMoneyMoney(node.attribute(attributeName(Attribute::Split::Shares))));
  split.setPrice(MyMoneyMoney(node.attribute(attributeName(Attribute::Split::Price))));
  split.setAccountId(node.attribute(attributeName(Attribute::Split::Account)));
  split.setCostCenterId(node.attribute(attributeName(Attribute::Split::CostCenter)));
  split.setNumber(node.attribute(attributeName(Attribute::Split::Number)));
  split.setBankID(node.attribute(attributeName(Attribute::Split::BankID)));

  auto xml = split.value(attributeName(Attribute::Split::KMMatchedTx));
  if (!xml.isEmpty()) {
    xml.replace(QLatin1String("&lt;"), QLatin1String("<"));
    QDomDocument docMatchedTransaction;
    QDomElement nodeMatchedTransaction;
    docMatchedTransaction.setContent(xml);
    nodeMatchedTransaction = docMatchedTransaction.documentElement().firstChild().toElement();
    auto t = MyMoneyXmlContentHandler::readTransaction(nodeMatchedTransaction);
    split.addMatch(t);
  }

  return split;
}

void MyMoneyXmlContentHandler::writeSplit(const MyMoneySplit &_split, QDomDocument &document, QDomElement &parent)
{
  auto el = document.createElement(elementName(Element::Split::Split));

  auto split = _split; // we need to convert matched transaction to kvp pair
  writeBaseXML(split.id(), document, el);

  el.setAttribute(attributeName(Attribute::Split::Payee), split.payeeId());
  //el.setAttribute(getAttrName(Split::Attribute::Tag), m_tag);
  el.setAttribute(attributeName(Attribute::Split::ReconcileDate), split.reconcileDate().toString(Qt::ISODate));
  el.setAttribute(attributeName(Attribute::Split::Action), split.action());
  el.setAttribute(attributeName(Attribute::Split::ReconcileFlag), (int)split.reconcileFlag());
  el.setAttribute(attributeName(Attribute::Split::Value), split.value().toString());
  el.setAttribute(attributeName(Attribute::Split::Shares), split.shares().toString());
  if (!split.price().isZero())
    el.setAttribute(attributeName(Attribute::Split::Price), split.price().toString());
  el.setAttribute(attributeName(Attribute::Split::Memo), split.memo());
  // No need to write the split id as it will be re-assigned when the file is read
  // el.setAttribute(getAttrName(Split::Attribute::ID), split.id());
  el.setAttribute(attributeName(Attribute::Split::Account), split.accountId());
  el.setAttribute(attributeName(Attribute::Split::Number), split.number());
  el.setAttribute(attributeName(Attribute::Split::BankID), split.bankID());
  if(!split.costCenterId().isEmpty())
    el.setAttribute(attributeName(Attribute::Split::CostCenter), split.costCenterId());
  const auto tagIdList = split.tagIdList();
  for (auto i = 0; i < tagIdList.count(); ++i) {
    QDomElement sel = document.createElement(elementName(Element::Split::Tag));
    sel.setAttribute(attributeName(Attribute::Split::ID), tagIdList[i]);
    el.appendChild(sel);
  }

  if (split.isMatched()) {
    QDomDocument docMatchedTransaction(elementName(Element::Split::Match));
    QDomElement elMatchedTransaction = docMatchedTransaction.createElement(elementName(Element::Split::Container));
    docMatchedTransaction.appendChild(elMatchedTransaction);
    writeTransaction(split.matchedTransaction(), docMatchedTransaction, elMatchedTransaction);
    auto xml = docMatchedTransaction.toString();
    xml.replace(QLatin1String("<"), QLatin1String("&lt;"));
    split.setValue(attributeName(Attribute::Split::KMMatchedTx), xml);
  } else {
    split.deletePair(attributeName(Attribute::Split::KMMatchedTx));
  }

  writeKeyValueContainer(split, document, el);

  parent.appendChild(el);
}

MyMoneyAccount MyMoneyXmlContentHandler::readAccount(const QDomElement &node)
{
  if (nodeName(Node::Account) != node.tagName())
    throw MYMONEYEXCEPTION_CSTRING("Node was not ACCOUNT");

  MyMoneyAccount acc(node.attribute(attributeName(Attribute::Account::ID)));

  addToKeyValueContainer(acc, node.elementsByTagName(nodeName(Node::KeyValuePairs)).item(0).toElement());

  acc.setName(node.attribute(attributeName(Attribute::Account::Name)));

  // qDebug("Reading information for account %s", acc.name().data());

  acc.setParentAccountId(node.attribute(attributeName(Attribute::Account::ParentAccount)));
  acc.setLastModified(QDate::fromString(node.attribute(attributeName(Attribute::Account::LastModified)), Qt::ISODate));
  acc.setLastReconciliationDate(QDate::fromString(node.attribute(attributeName(Attribute::Account::LastReconciled)), Qt::ISODate));

  // Very old versions of KMyMoney used to store the reconciliation date in
  // the KVP as "lastStatementDate". Since we don't use it anymore, we get
  // rid of it in case we read such an old file.
  acc.deletePair(QStringLiteral("lastStatementDate"));

  acc.setInstitutionId(node.attribute(attributeName(Attribute::Account::Institution)));
  acc.setNumber(node.attribute(attributeName(Attribute::Account::Number)));
  acc.setOpeningDate(QDate::fromString(node.attribute(attributeName(Attribute::Account::Opened)), Qt::ISODate));
  acc.setCurrencyId(node.attribute(attributeName(Attribute::Account::Currency)));

  auto tmp = node.attribute(attributeName(Attribute::Account::Type));
  auto bOK = false;
  auto type = tmp.toInt(&bOK);
  if (bOK) {
    acc.setAccountType(static_cast<Account::Type>(type));
  } else {
    qWarning("XMLREADER: Account %s had invalid or no account type information.", qPrintable(acc.name()));
  }

  const auto openingBalance = node.attribute(attributeName(Attribute::Account::OpeningBalance));
  if (!openingBalance.isEmpty())
    if (!MyMoneyMoney(openingBalance).isZero())
      throw MYMONEYEXCEPTION(QString::fromLatin1("Account %1 contains an opening balance. Please use KMyMoney version 0.8 or later and earlier than version 0.9 to correct the problem.").arg(acc.name()));

  acc.setDescription(node.attribute(attributeName(Attribute::Account::Description)));

  // qDebug("Account %s has id of %s, type of %d, parent is %s.", acc.name().data(), id.data(), type, acc.parentAccountId().data());

  //  Process any Sub-Account information found inside the account entry.
  acc.removeAccountIds();
  QDomNodeList nodeList = node.elementsByTagName(elementName(Element::Account::SubAccounts));
  if (!nodeList.isEmpty()) {
    nodeList = nodeList.item(0).toElement().elementsByTagName(elementName(Element::Account::SubAccount));
    for (int i = 0; i < nodeList.count(); ++i) {
      acc.addAccountId(QString(nodeList.item(i).toElement().attribute(attributeName(Attribute::Account::ID))));
    }
  }

  nodeList = node.elementsByTagName(elementName(Element::Account::OnlineBanking));
  if (!nodeList.isEmpty()) {
    MyMoneyKeyValueContainer kvp;
    const auto attributes = nodeList.item(0).toElement().attributes();
    for (auto i = 0; i < attributes.count(); ++i) {
      const auto it_attr = attributes.item(i).toAttr();
      kvp.setValue(it_attr.name(), it_attr.value());
    }
    acc.setOnlineBankingSettings(kvp);
  }

  // Up to and including version 4.6.6 the new account dialog stored the iban in the kvp-key "IBAN".
  // But the rest of the software uses "iban". So correct this:
  if (!acc.value("IBAN").isEmpty()) {
    // If "iban" was not set, set it now. If it is set, the user reseted it already, so remove
    // the garbage.
    if (acc.value(attributeName(Attribute::Account::IBAN)).isEmpty())
      acc.setValue(attributeName(Attribute::Account::IBAN), acc.value("IBAN"));
    acc.deletePair("IBAN");
  }
  return acc;
}

void MyMoneyXmlContentHandler::writeAccount(const MyMoneyAccount &account, QDomDocument &document, QDomElement &parent)
{
  auto el = document.createElement(nodeName(Node::Account));

  writeBaseXML(account.id(), document, el);

  el.setAttribute(attributeName(Attribute::Account::ParentAccount), account.parentAccountId());
  el.setAttribute(attributeName(Attribute::Account::LastReconciled), MyMoneyUtils::dateToString(account.lastReconciliationDate()));
  el.setAttribute(attributeName(Attribute::Account::LastModified), MyMoneyUtils::dateToString(account.lastModified()));
  el.setAttribute(attributeName(Attribute::Account::Institution), account.institutionId());
  el.setAttribute(attributeName(Attribute::Account::Opened), MyMoneyUtils::dateToString(account.openingDate()));
  el.setAttribute(attributeName(Attribute::Account::Number), account.number());
  // el.setAttribute(getAttrName(anOpeningBalance), account.openingBalance().toString());
  el.setAttribute(attributeName(Attribute::Account::Type), (int)account.accountType());
  el.setAttribute(attributeName(Attribute::Account::Name), account.name());
  el.setAttribute(attributeName(Attribute::Account::Description), account.description());
  if (!account.currencyId().isEmpty())
    el.setAttribute(attributeName(Attribute::Account::Currency), account.currencyId());

  //Add in subaccount information, if this account has subaccounts.
  if (account.accountCount()) {
    QDomElement subAccounts = document.createElement(elementName(Element::Account::SubAccounts));
    foreach (const auto accountID, account.accountList()) {
      QDomElement temp = document.createElement(elementName(Element::Account::SubAccount));
      temp.setAttribute(attributeName(Attribute::Account::ID), accountID);
      subAccounts.appendChild(temp);
    }

    el.appendChild(subAccounts);
  }

  // Write online banking settings
  auto onlineBankSettingsPairs = account.onlineBankingSettings().pairs();
  if (onlineBankSettingsPairs.count()) {
    QDomElement onlinesettings = document.createElement(elementName(Element::Account::OnlineBanking));
    QMap<QString, QString>::const_iterator it_key = onlineBankSettingsPairs.constBegin();
    while (it_key != onlineBankSettingsPairs.constEnd()) {
      onlinesettings.setAttribute(it_key.key(), it_key.value());
      ++it_key;
    }
    el.appendChild(onlinesettings);
  }

  //Add in Key-Value Pairs for accounts.
  writeKeyValueContainer(account, document, el);

  parent.appendChild(el);
}

MyMoneyPayee MyMoneyXmlContentHandler::readPayee(const QDomElement &node)
{
  if (nodeName(Node::Payee) != node.tagName())
    throw MYMONEYEXCEPTION_CSTRING("Node was not PAYEE");

  MyMoneyPayee payee(node.attribute(attributeName(Attribute::Account::ID)));

  //  MyMoneyPayee payee(node.attribute(attributeName(Attribute::Payee::ID)));
  payee.setName(node.attribute(attributeName(Attribute::Payee::Name)));
  payee.setReference(node.attribute(attributeName(Attribute::Payee::Reference)));
  payee.setEmail(node.attribute(attributeName(Attribute::Payee::Email)));
  auto type = static_cast<eMyMoney::Payee::MatchType>(node.attribute(attributeName(Attribute::Payee::MatchingEnabled), "0").toUInt());
  payee.setMatchData(eMyMoney::Payee::MatchType::Disabled, true, QString());
  if (type != eMyMoney::Payee::MatchType::Disabled) {
    payee.setMatchData((node.attribute(attributeName(Attribute::Payee::UsingMatchKey), "0").toUInt() != 0) ? eMyMoney::Payee::MatchType::Key : eMyMoney::Payee::MatchType::Name,
                       node.attribute(attributeName(Attribute::Payee::MatchIgnoreCase), "0").toUInt(),
                       node.attribute(attributeName(Attribute::Payee::MatchKey)));
  }

  if (node.hasAttribute(attributeName(Attribute::Payee::Notes)))
    payee.setNotes(node.attribute(attributeName(Attribute::Payee::Notes)));

  if (node.hasAttribute(attributeName(Attribute::Payee::DefaultAccountID)))
    payee.setDefaultAccountId(node.attribute(attributeName(Attribute::Payee::DefaultAccountID)));

  // Load Address
  QDomNodeList nodeList = node.elementsByTagName(elementName(Element::Payee::Address));
  if (nodeList.isEmpty())
    throw MYMONEYEXCEPTION(QString::fromLatin1("No ADDRESS in payee %1").arg(payee.name()));

  QDomElement addrNode = nodeList.item(0).toElement();
  payee.setAddress(addrNode.attribute(attributeName(Attribute::Payee::Street)));
  payee.setCity(addrNode.attribute(attributeName(Attribute::Payee::City)));
  payee.setPostcode(addrNode.attribute(attributeName(Attribute::Payee::PostCode)));
  payee.setState(addrNode.attribute(attributeName(Attribute::Payee::State)));
  payee.setTelephone(addrNode.attribute(attributeName(Attribute::Payee::Telephone)));

  payee.loadXML(node);

  return payee;
}

void MyMoneyXmlContentHandler::writePayee(const MyMoneyPayee &payee, QDomDocument &document, QDomElement &parent)
{
  auto el = document.createElement(nodeName(Node::Payee));

  writeBaseXML(payee.id(), document, el);

  el.setAttribute(attributeName(Attribute::Payee::Name), payee.name());
  el.setAttribute(attributeName(Attribute::Payee::Reference), payee.reference());
  el.setAttribute(attributeName(Attribute::Payee::Email), payee.email());
  if (!payee.notes().isEmpty())
    el.setAttribute(attributeName(Attribute::Payee::Notes), payee.notes());

  el.setAttribute(attributeName(Attribute::Payee::MatchingEnabled), payee.isMatchingEnabled());
  if (payee.isMatchingEnabled()) {
    el.setAttribute(attributeName(Attribute::Payee::UsingMatchKey), payee.isUsingMatchKey());
    el.setAttribute(attributeName(Attribute::Payee::MatchIgnoreCase), payee.isMatchKeyIgnoreCase());
    el.setAttribute(attributeName(Attribute::Payee::MatchKey), payee.matchKey());
  }

  if (!payee.defaultAccountId().isEmpty()) {
    el.setAttribute(attributeName(Attribute::Payee::DefaultAccountID), payee.defaultAccountId());
  }

  // Save address
  QDomElement address = document.createElement(elementName(Element::Payee::Address));
  address.setAttribute(attributeName(Attribute::Payee::Street), payee.address());
  address.setAttribute(attributeName(Attribute::Payee::City), payee.city());
  address.setAttribute(attributeName(Attribute::Payee::PostCode), payee.postcode());
  address.setAttribute(attributeName(Attribute::Payee::State), payee.state());
  address.setAttribute(attributeName(Attribute::Payee::Telephone), payee.telephone());

  el.appendChild(address);

  // Save payeeIdentifiers (account numbers)
  payee.MyMoneyPayeeIdentifierContainer::writeXML(document, el);

  parent.appendChild(el);
}

MyMoneyTag MyMoneyXmlContentHandler::readTag(const QDomElement &node)
{
  if (nodeName(Node::Tag) != node.tagName())
    throw MYMONEYEXCEPTION_CSTRING("Node was not TAG");

  MyMoneyTag tag(node.attribute(attributeName(Attribute::Account::ID)));

  tag.setName(node.attribute(attributeName(Attribute::Tag::Name)));
  if (node.hasAttribute(attributeName(Attribute::Tag::TagColor))) {
    tag.setNamedTagColor(attributeName(Attribute::Tag::TagColor));
  }
  if (node.hasAttribute(attributeName(Attribute::Tag::Notes))) {
    tag.setNotes(node.attribute(attributeName(Attribute::Tag::Notes)));
  }
  tag.setClosed(node.attribute(attributeName(Attribute::Tag::Closed), "0").toUInt());

  return tag;
}

void MyMoneyXmlContentHandler::writeTag(const MyMoneyTag &tag, QDomDocument &document, QDomElement &parent)
{
  auto el = document.createElement(nodeName(Node::Tag));

  writeBaseXML(tag.id(), document, el);

  el.setAttribute(attributeName(Attribute::Tag::Name), tag.name());
  el.setAttribute(attributeName(Attribute::Tag::Closed), tag.isClosed());
  if (tag.tagColor().isValid())
    el.setAttribute(attributeName(Attribute::Tag::TagColor), tag.tagColor().name());
  if (!tag.notes().isEmpty())
    el.setAttribute(attributeName(Attribute::Tag::Notes), tag.notes());
  parent.appendChild(el);
}

MyMoneySecurity MyMoneyXmlContentHandler::readSecurity(const QDomElement &node)
{
  const auto tag = node.tagName();
  if ((nodeName(Node::Security) != tag)
      && (nodeName(Node::Equity) != tag)
      && (nodeName(Node::Currency) != tag))
    throw MYMONEYEXCEPTION_CSTRING("Node was not SECURITY or CURRENCY");

  MyMoneySecurity security(node.attribute(attributeName(Attribute::Account::ID)));

  addToKeyValueContainer(security, node.elementsByTagName(nodeName(Node::KeyValuePairs)).item(0).toElement());

  security.setName(node.attribute(attributeName(Attribute::Security::Name)));
  security.setTradingSymbol(node.attribute(attributeName(Attribute::Security::Symbol)));
  security.setSecurityType(static_cast<eMyMoney::Security::Type>(node.attribute(attributeName(Attribute::Security::Type)).toInt()));
  security.setRoundingMethod(static_cast<AlkValue::RoundingMethod>(node.attribute(attributeName(Attribute::Security::RoundingMethod)).toInt()));
  security.setSmallestAccountFraction(node.attribute(attributeName(Attribute::Security::SAF)).toUInt());
  security.setPricePrecision(node.attribute(attributeName(Attribute::Security::PP)).toUInt());

  if (security.smallestAccountFraction() == 0)
    security.setSmallestAccountFraction(100);
  if (security.pricePrecision() == 0 || security.pricePrecision() > 10)
    security.setPricePrecision(4);

  if (security.isCurrency()) {
    security.setSmallestCashFraction(node.attribute(attributeName(Attribute::Security::SCF)).toUInt());
    if (security.smallestCashFraction() == 0)
      security.setSmallestCashFraction(100);
  } else {
    security.setTradingCurrency(node.attribute(attributeName(Attribute::Security::TradingCurrency)));
    security.setTradingMarket(node.attribute(attributeName(Attribute::Security::TradingMarket)));
  }

  return security;
}

void MyMoneyXmlContentHandler::writeSecurity(const MyMoneySecurity &security, QDomDocument &document, QDomElement &parent)
{
  QDomElement el;
  if (security.isCurrency())
    el = document.createElement(nodeName(Node::Currency));
  else
    el = document.createElement(nodeName(Node::Security));

  writeBaseXML(security.id(), document, el);

  el.setAttribute(attributeName(Attribute::Security::Name), security.name());
  el.setAttribute(attributeName(Attribute::Security::Symbol),security.tradingSymbol());
  el.setAttribute(attributeName(Attribute::Security::Type), static_cast<int>(security.securityType()));
  el.setAttribute(attributeName(Attribute::Security::RoundingMethod), static_cast<int>(security.roundingMethod()));
  el.setAttribute(attributeName(Attribute::Security::SAF), security.smallestAccountFraction());
  el.setAttribute(attributeName(Attribute::Security::PP), security.pricePrecision());
  if (security.isCurrency())
    el.setAttribute(attributeName(Attribute::Security::SCF), security.smallestCashFraction());
  else {
    el.setAttribute(attributeName(Attribute::Security::TradingCurrency), security.tradingCurrency());
    el.setAttribute(attributeName(Attribute::Security::TradingMarket), security.tradingMarket());
  }

  //Add in Key-Value Pairs for securities.
  writeKeyValueContainer(security, document, el);

  parent.appendChild(el);
}

MyMoneyInstitution MyMoneyXmlContentHandler::readInstitution(const QDomElement &node)
{
  if (nodeName(Node::Institution) != node.tagName())
    throw MYMONEYEXCEPTION_CSTRING("Node was not INSTITUTION");

  MyMoneyInstitution institution(node.attribute(attributeName(Attribute::Account::ID)));

  addToKeyValueContainer(institution, node.elementsByTagName(nodeName(Node::KeyValuePairs)).item(0).toElement());

  institution.setSortcode(node.attribute(attributeName(Attribute::Institution::SortCode)));
  institution.setName(node.attribute(attributeName(Attribute::Institution::Name)));
  institution.setManager(node.attribute(attributeName(Attribute::Institution::Manager)));

  QDomNodeList nodeList = node.elementsByTagName(elementName(Element::Institution::Address));
  if (nodeList.isEmpty())
    throw MYMONEYEXCEPTION(QString::fromLatin1("No ADDRESS in institution %1").arg(institution.name()));

  QDomElement addrNode = nodeList.item(0).toElement();
  institution.setStreet(addrNode.attribute(attributeName(Attribute::Institution::Street)));
  institution.setTown(addrNode.attribute(attributeName(Attribute::Institution::City)));
  institution.setPostcode(addrNode.attribute(attributeName(Attribute::Institution::Zip)));
  institution.setTelephone(addrNode.attribute(attributeName(Attribute::Institution::Telephone)));

  nodeList = node.elementsByTagName(elementName(Element::Institution::AccountIDS));
  if (!nodeList.isEmpty()) {
    nodeList = nodeList.item(0).toElement().elementsByTagName(elementName(Element::Institution::AccountID));
    for (int i = 0; i < nodeList.count(); ++i)
      institution.addAccountId(nodeList.item(i).toElement().attribute(attributeName(Attribute::Institution::ID)));
  }

  return institution;
}

void MyMoneyXmlContentHandler::writeInstitution(const MyMoneyInstitution &institution, QDomDocument &document, QDomElement &parent)
{
  auto el = document.createElement(nodeName(Node::Institution));

  writeBaseXML(institution.id(), document, el);

  el.setAttribute(attributeName(Attribute::Institution::Name), institution.name());
  el.setAttribute(attributeName(Attribute::Institution::Manager), institution.manager());
  el.setAttribute(attributeName(Attribute::Institution::SortCode), institution.sortcode());

  auto address = document.createElement(elementName(Element::Institution::Address));
  address.setAttribute(attributeName(Attribute::Institution::Street), institution.street());
  address.setAttribute(attributeName(Attribute::Institution::City), institution.town());
  address.setAttribute(attributeName(Attribute::Institution::Zip), institution.postcode());
  address.setAttribute(attributeName(Attribute::Institution::Telephone), institution.telephone());
  el.appendChild(address);

  auto accounts = document.createElement(elementName(Element::Institution::AccountIDS));
  foreach (const auto accountID, institution.accountList()) {
    auto temp = document.createElement(elementName(Element::Institution::AccountID));
    temp.setAttribute(attributeName(Attribute::Institution::ID), accountID);
    accounts.appendChild(temp);
  }
  el.appendChild(accounts);

  //Add in Key-Value Pairs for institutions.
  writeKeyValueContainer(institution, document, el);

  parent.appendChild(el);
}

MyMoneySchedule MyMoneyXmlContentHandler::readSchedule(const QDomElement &node)
{
  if (nodeName(Node::ScheduleTX) != node.tagName())
    throw MYMONEYEXCEPTION_CSTRING("Node was not SCHEDULED_TX");

  MyMoneySchedule schedule(node.attribute(attributeName(Attribute::Account::ID)));

  schedule.setName(node.attribute(attributeName(Attribute::Schedule::Name)));
  schedule.setStartDate(MyMoneyUtils::stringToDate(node.attribute(attributeName(Attribute::Schedule::StartDate))));
  schedule.setEndDate(MyMoneyUtils::stringToDate(node.attribute(attributeName(Attribute::Schedule::EndDate))));
  schedule.setLastPayment(MyMoneyUtils::stringToDate(node.attribute(attributeName(Attribute::Schedule::LastPayment))));

  schedule.setType(static_cast<Schedule::Type>(node.attribute(attributeName(Attribute::Schedule::Type)).toInt()));
  schedule.setPaymentType(static_cast<Schedule::PaymentType>(node.attribute(attributeName(Attribute::Schedule::PaymentType)).toInt()));
  schedule.setOccurrence(static_cast<Schedule::Occurrence>(node.attribute(attributeName(Attribute::Schedule::Occurrence)).toInt()));
  schedule.setOccurrenceMultiplier(node.attribute(attributeName(Attribute::Schedule::OccurrenceMultiplier), "1").toInt());
  schedule.setLastDayInMonth(static_cast<bool>(node.attribute("lastDayInMonth").toInt()));
  schedule.setAutoEnter(static_cast<bool>(node.attribute(attributeName(Attribute::Schedule::AutoEnter)).toInt()));
  schedule.setFixed(static_cast<bool>(node.attribute(attributeName(Attribute::Schedule::Fixed)).toInt()));
  schedule.setWeekendOption(static_cast<Schedule::WeekendOption>(node.attribute(attributeName(Attribute::Schedule::WeekendOption)).toInt()));

  // read in the associated transaction
  QDomNodeList nodeList = node.elementsByTagName(nodeName(Node::Transaction));
  if (nodeList.count() == 0)
    throw MYMONEYEXCEPTION_CSTRING("SCHEDULED_TX has no TRANSACTION node");

  auto transaction = readTransaction(nodeList.item(0).toElement(), false);

  // some old versions did not remove the entry date and post date fields
  // in the schedule. So if this is the case, we deal with a very old transaction
  // and can't use the post date field as next due date. Hence, we wipe it out here
  if (transaction.entryDate().isValid()) {
    transaction.setPostDate(QDate());
    transaction.setEntryDate(QDate());
  }
  schedule.setTransaction(transaction, true);

  // readin the recorded payments
  nodeList = node.elementsByTagName(elementName(Element::Schedule::Payments));
  if (!nodeList.isEmpty()) {
    nodeList = nodeList.item(0).toElement().elementsByTagName(elementName(Element::Schedule::Payment));
    for (int i = 0; i < nodeList.count(); ++i) {
      schedule.recordPayment(MyMoneyUtils::stringToDate(nodeList.item(i).toElement().attribute(attributeName(Attribute::Schedule::Date))));
    }
  }

  // if the next due date is not set (comes from old version)
  // then set it up the old way
  if (!schedule.nextDueDate().isValid() && !schedule.lastPayment().isValid()) {
    auto t = schedule.transaction();
    t.setPostDate(schedule.startDate());
    schedule.setTransaction(t, true);
    // clear it, because the schedule has never been used
    schedule.setStartDate(QDate());
  }

  // There are reports that lastPayment and nextDueDate are identical or
  // that nextDueDate is older than lastPayment. This could
  // be caused by older versions of the application. In this case, we just
  // clear out the nextDueDate and let it calculate from the lastPayment.
  if (schedule.nextDueDate().isValid() && schedule.nextDueDate() <= schedule.lastPayment()) {
    auto t = schedule.transaction();
    t.setPostDate(QDate());
    schedule.setTransaction(t, true);
  }

  if (!schedule.nextDueDate().isValid()) {
    auto t = schedule.transaction();
    t.setPostDate(schedule.startDate());
    schedule.setTransaction(t, true);
    t = schedule.transaction();
    t.setPostDate(schedule.nextPayment(schedule.lastPayment().addDays(1)));
    schedule.setTransaction(t, true);
  }

  return schedule;
}

void MyMoneyXmlContentHandler::writeSchedule(const MyMoneySchedule &schedule, QDomDocument &document, QDomElement &parent)
{
  auto el = document.createElement(nodeName(Node::ScheduleTX));

  writeBaseXML(schedule.id(), document, el);

  el.setAttribute(attributeName(Attribute::Schedule::Name), schedule.name());
  el.setAttribute(attributeName(Attribute::Schedule::Type), (int)schedule.type());
  el.setAttribute(attributeName(Attribute::Schedule::Occurrence), (int)schedule.occurrence());
  el.setAttribute(attributeName(Attribute::Schedule::OccurrenceMultiplier), schedule.occurrenceMultiplier());
  el.setAttribute(attributeName(Attribute::Schedule::PaymentType), (int)schedule.paymentType());
  el.setAttribute(attributeName(Attribute::Schedule::StartDate), MyMoneyUtils::dateToString(schedule.startDate()));
  el.setAttribute(attributeName(Attribute::Schedule::EndDate), MyMoneyUtils::dateToString(schedule.endDate()));
  el.setAttribute(attributeName(Attribute::Schedule::Fixed), schedule.isFixed());
  el.setAttribute(attributeName(Attribute::Schedule::LastDayInMonth), schedule.lastDayInMonth());
  el.setAttribute(attributeName(Attribute::Schedule::AutoEnter), schedule.autoEnter());
  el.setAttribute(attributeName(Attribute::Schedule::LastPayment), MyMoneyUtils::dateToString(schedule.lastPayment()));
  el.setAttribute(attributeName(Attribute::Schedule::WeekendOption), (int)schedule.weekendOption());

  //store the payment history for this scheduled task.
  QList<QDate> payments = schedule.recordedPayments();
  QList<QDate>::ConstIterator it;
  QDomElement paymentsElement = document.createElement(elementName(Element::Schedule::Payments));
  for (it = payments.constBegin(); it != payments.constEnd(); ++it) {
    QDomElement paymentEntry = document.createElement(elementName(Element::Schedule::Payment));
    paymentEntry.setAttribute(attributeName(Attribute::Schedule::Date), MyMoneyUtils::dateToString(*it));
    paymentsElement.appendChild(paymentEntry);
  }
  el.appendChild(paymentsElement);

  //store the transaction data for this task.
  writeTransaction(schedule.transaction(), document, el);

  parent.appendChild(el);
}

onlineJob MyMoneyXmlContentHandler::readOnlineJob(const QDomElement &node)
{
  onlineJob oJob(node.attribute(attributeName(Attribute::Account::ID)));

  oJob.clearJobMessageList();
  oJob.setLock(false);
  oJob.setJobSend(QDateTime::fromString(node.attribute(attributeName(Attribute::OnlineJob::Send)), Qt::ISODate));
  const auto state = node.attribute(attributeName(Attribute::OnlineJob::BankAnswerState));
  const auto date = QDateTime::fromString(node.attribute(attributeName(Attribute::OnlineJob::BankAnswerDate)), Qt::ISODate);
  if (state == attributeName(Attribute::OnlineJob::AbortedByUser))
    oJob.setBankAnswer(eMyMoney::OnlineJob::sendingState::abortedByUser, date);
  else if (state == attributeName(Attribute::OnlineJob::AcceptedByBank))
    oJob.setBankAnswer(eMyMoney::OnlineJob::sendingState::acceptedByBank, date);
  else if (state == attributeName(Attribute::OnlineJob::RejectedByBank))
      oJob.setBankAnswer(eMyMoney::OnlineJob::sendingState::rejectedByBank, date);
  else if (state == attributeName(Attribute::OnlineJob::SendingError))
      oJob.setBankAnswer(eMyMoney::OnlineJob::sendingState::sendingError, date);
  else
    oJob.setBankAnswer(eMyMoney::OnlineJob::sendingState::noBankAnswer);

  auto taskElem = node.firstChildElement(elementName(Element::OnlineJob::OnlineTask));
  oJob.setTask(onlineJobAdministration::instance()->createOnlineTaskByXml(taskElem.attribute(attributeName(Attribute::OnlineJob::IID)), taskElem));

  return oJob;
}

void MyMoneyXmlContentHandler::writeOnlineJob(const onlineJob &job, QDomDocument &document, QDomElement &parent)
{
  auto el = document.createElement(nodeName(Node::OnlineJob));

  writeBaseXML(job.id(), document, el);

  if (!job.sendDate().isNull())
    el.setAttribute(attributeName(Attribute::OnlineJob::Send), job.sendDate().toString(Qt::ISODate));
  if (!job.bankAnswerDate().isNull())
    el.setAttribute(attributeName(Attribute::OnlineJob::BankAnswerDate), job.bankAnswerDate().toString(Qt::ISODate));

  switch (job.bankAnswerState()) {
    case eMyMoney::OnlineJob::sendingState::abortedByUser: el.setAttribute(attributeName(Attribute::OnlineJob::BankAnswerState), attributeName(Attribute::OnlineJob::AbortedByUser)); break;
    case eMyMoney::OnlineJob::sendingState::acceptedByBank: el.setAttribute(attributeName(Attribute::OnlineJob::BankAnswerState), attributeName(Attribute::OnlineJob::AcceptedByBank)); break;
    case eMyMoney::OnlineJob::sendingState::rejectedByBank: el.setAttribute(attributeName(Attribute::OnlineJob::BankAnswerState), attributeName(Attribute::OnlineJob::RejectedByBank)); break;
    case eMyMoney::OnlineJob::sendingState::sendingError: el.setAttribute(attributeName(Attribute::OnlineJob::BankAnswerState), attributeName(Attribute::OnlineJob::SendingError)); break;
    case eMyMoney::OnlineJob::sendingState::noBankAnswer:
    default: void();
  }

  QDomElement taskEl = document.createElement(elementName(Element::OnlineJob::OnlineTask));
  taskEl.setAttribute(attributeName(Attribute::OnlineJob::IID), job.taskIid());
  try {
    job.task()->writeXML(document, taskEl); // throws execption if there is no task
    el.appendChild(taskEl); // only append child if there is something to append
  } catch (const onlineJob::emptyTask &) {
  }

  parent.appendChild(el);
}

MyMoneyCostCenter MyMoneyXmlContentHandler::readCostCenter(const QDomElement &node)
{
  if (nodeName(Node::CostCenter) != node.tagName())
    throw MYMONEYEXCEPTION_CSTRING("Node was not COSTCENTER");

  MyMoneyCostCenter costCenter(node.attribute(attributeName(Attribute::Account::ID)));
  costCenter.setName(node.attribute(attributeName(Attribute::CostCenter::Name)));
  return costCenter;
}

void MyMoneyXmlContentHandler::writeCostCenter(const MyMoneyCostCenter &costCenter, QDomDocument &document, QDomElement &parent)
{
  auto el = document.createElement(nodeName(Node::CostCenter));

  writeBaseXML(costCenter.id(), document, el);

  el.setAttribute(attributeName(Attribute::CostCenter::Name), costCenter.name());
  parent.appendChild(el);
}






MyMoneyStorageXML::MyMoneyStorageXML() :
    m_progressCallback(0),
    m_storage(0),
    m_doc(0),
    d(new Private())
{
}

MyMoneyStorageXML::~MyMoneyStorageXML()
{
  delete d;
}

// Function to read in the file, send to XML parser.
void MyMoneyStorageXML::readFile(QIODevice* pDevice, MyMoneyStorageMgr* storage)
{
  Q_CHECK_PTR(storage);
  Q_CHECK_PTR(pDevice);
  if (!storage)
    return;

  m_storage = storage;

  m_doc = new QDomDocument;
  Q_CHECK_PTR(m_doc);

  qDebug("reading file");
  // creating the QXmlInputSource object based on a QIODevice object
  // reads all data of the underlying object into memory. I have not
  // found an object that reads on the fly. I tried to derive one myself,
  // but there could be a severe problem with decoding when reading
  // blocks of data and not a stream. So I left it the way it is. (ipwizard)
  QXmlInputSource xml(pDevice);

  qDebug("start parsing file");
  MyMoneyXmlContentHandler mmxml(this);
  QXmlSimpleReader reader;
  reader.setContentHandler(&mmxml);

  if (!reader.parse(&xml, false)) {
    delete m_doc;
    m_doc = 0;
    signalProgress(-1, -1);
    throw MYMONEYEXCEPTION_CSTRING("File was not parsable!");
  }

  // check if we need to build up the account balances
  if (fileVersionRead < 2)
    m_storage->rebuildAccountBalances();

  delete m_doc;
  m_doc = 0;

  // this seems to be nonsense, but it clears the dirty flag
  // as a side-effect.
  m_storage->setLastModificationDate(m_storage->lastModificationDate());
  m_storage = 0;

  //hides the progress bar.
  signalProgress(-1, -1);
}

void MyMoneyStorageXML::writeFile(QIODevice* qf, MyMoneyStorageMgr* storage)
{
  Q_CHECK_PTR(qf);
  Q_CHECK_PTR(storage);
  if (!storage) {
    return;
  }
  m_storage = storage;

  // qDebug("XMLWRITER: Starting file write");
  m_doc = new QDomDocument(tagName(Tag::KMMFile));
  Q_CHECK_PTR(m_doc);
  QDomProcessingInstruction instruct = m_doc->createProcessingInstruction("xml", "version=\"1.0\" encoding=\"utf-8\"");
  m_doc->appendChild(instruct);

  QDomElement mainElement = m_doc->createElement(tagName(Tag::KMMFile));
  m_doc->appendChild(mainElement);

  QDomElement fileInfo = m_doc->createElement(tagName(Tag::FileInfo));
  writeFileInformation(fileInfo);
  mainElement.appendChild(fileInfo);

  QDomElement userInfo = m_doc->createElement(tagName(Tag::User));
  writeUserInformation(userInfo);
  mainElement.appendChild(userInfo);

  QDomElement institutions = m_doc->createElement(tagName(Tag::Institutions));
  writeInstitutions(institutions);
  mainElement.appendChild(institutions);

  QDomElement payees = m_doc->createElement(tagName(Tag::Payees));
  writePayees(payees);
  mainElement.appendChild(payees);

  QDomElement costCenters = m_doc->createElement(tagName(Tag::CostCenters));
  writeCostCenters(costCenters);
  mainElement.appendChild(costCenters);

  QDomElement tags = m_doc->createElement(tagName(Tag::Tags));
  writeTags(tags);
  mainElement.appendChild(tags);

  QDomElement accounts = m_doc->createElement(tagName(Tag::Accounts));
  writeAccounts(accounts);
  mainElement.appendChild(accounts);

  QDomElement transactions = m_doc->createElement(tagName(Tag::Transactions));
  writeTransactions(transactions);
  mainElement.appendChild(transactions);

  QDomElement keyvalpairs = writeKeyValuePairs(m_storage->pairs());
  mainElement.appendChild(keyvalpairs);

  QDomElement schedules = m_doc->createElement(tagName(Tag::Schedules));
  writeSchedules(schedules);
  mainElement.appendChild(schedules);

  QDomElement equities = m_doc->createElement(tagName(Tag::Securities));
  writeSecurities(equities);
  mainElement.appendChild(equities);

  QDomElement currencies = m_doc->createElement(tagName(Tag::Currencies));
  writeCurrencies(currencies);
  mainElement.appendChild(currencies);

  QDomElement prices = m_doc->createElement(tagName(Tag::Prices));
  writePrices(prices);
  mainElement.appendChild(prices);

  QDomElement reports = m_doc->createElement(tagName(Tag::Reports));
  writeReports(reports);
  mainElement.appendChild(reports);

  QDomElement budgets = m_doc->createElement(tagName(Tag::Budgets));
  writeBudgets(budgets);
  mainElement.appendChild(budgets);

  QDomElement onlineJobs = m_doc->createElement(tagName(Tag::OnlineJobs));
  writeOnlineJobs(onlineJobs);
  mainElement.appendChild(onlineJobs);

  QTextStream stream(qf);
  stream.setCodec("UTF-8");
  stream << m_doc->toString();

  delete m_doc;
  m_doc = 0;

  //hides the progress bar.
  signalProgress(-1, -1);

  // this seems to be nonsense, but it clears the dirty flag
  // as a side-effect.
  m_storage->setLastModificationDate(m_storage->lastModificationDate());

  m_storage = 0;
}

bool MyMoneyStorageXML::readFileInformation(const QDomElement& fileInfo)
{
  signalProgress(0, 3, i18n("Loading file information..."));
  bool rc = true;
  QDomElement temp = findChildElement(elementName(Element::General::CreationDate), fileInfo);
  if (temp == QDomElement()) {
    rc = false;
  }
  QString strDate = MyMoneyUtils::QStringEmpty(temp.attribute(attributeName(Attribute::General::Date)));
  m_storage->setCreationDate(MyMoneyUtils::stringToDate(strDate));
  signalProgress(1, 0);

  temp = findChildElement(elementName(Element::General::LastModifiedDate), fileInfo);
  if (temp == QDomElement()) {
    rc = false;
  }
  strDate = MyMoneyUtils::QStringEmpty(temp.attribute(attributeName(Attribute::General::Date)));
  m_storage->setLastModificationDate(MyMoneyUtils::stringToDate(strDate));
  signalProgress(2, 0);

  temp = findChildElement(elementName(Element::General::Version), fileInfo);
  if (temp == QDomElement()) {
    rc = false;
  }
  QString strVersion = MyMoneyUtils::QStringEmpty(temp.attribute(attributeName(Attribute::General::ID)));
  fileVersionRead = strVersion.toUInt(0, 16);

  temp = findChildElement(elementName(Element::General::FixVersion), fileInfo);
  if (temp != QDomElement()) {
    QString strFixVersion = MyMoneyUtils::QStringEmpty(temp.attribute(attributeName(Attribute::General::ID)));
    m_storage->setFileFixVersion(strFixVersion.toUInt());
    // skip KMyMoneyView::fixFile_2()
    if (m_storage->fileFixVersion() == 2) {
      m_storage->setFileFixVersion(3);
    }
  }
  // FIXME The old version stuff used this rather odd number
  //       We now use increments
  if (fileVersionRead == VERSION_0_60_XML)
    fileVersionRead = 1;
  signalProgress(3, 0);

  return rc;
}

void MyMoneyStorageXML::writeFileInformation(QDomElement& fileInfo)
{
  QDomElement creationDate = m_doc->createElement(elementName(Element::General::CreationDate));
  creationDate.setAttribute(attributeName(Attribute::General::Date), MyMoneyUtils::dateToString(m_storage->creationDate()));
  fileInfo.appendChild(creationDate);

  QDomElement lastModifiedDate = m_doc->createElement(elementName(Element::General::LastModifiedDate));
  lastModifiedDate.setAttribute(attributeName(Attribute::General::Date), MyMoneyUtils::dateToString(m_storage->lastModificationDate()));
  fileInfo.appendChild(lastModifiedDate);

  QDomElement version = m_doc->createElement(elementName(Element::General::Version));

  version.setAttribute(attributeName(Attribute::General::ID), "1");
  fileInfo.appendChild(version);

  QDomElement fixVersion = m_doc->createElement(elementName(Element::General::FixVersion));
  fixVersion.setAttribute(attributeName(Attribute::General::ID), m_storage->fileFixVersion());
  fileInfo.appendChild(fixVersion);
}

void MyMoneyStorageXML::writeUserInformation(QDomElement& userInfo)
{
  MyMoneyPayee user = m_storage->user();
  userInfo.setAttribute(attributeName(Attribute::General::Name), user.name());
  userInfo.setAttribute(attributeName(Attribute::General::Email), user.email());

  QDomElement address = m_doc->createElement(elementName(Element::General::Address));
  address.setAttribute(attributeName(Attribute::General::Street), user.address());
  address.setAttribute(attributeName(Attribute::General::City), user.city());
  address.setAttribute(attributeName(Attribute::General::Country), user.state());
  address.setAttribute(attributeName(Attribute::General::ZipCode), user.postcode());
  address.setAttribute(attributeName(Attribute::General::Telephone), user.telephone());

  userInfo.appendChild(address);
}

bool MyMoneyStorageXML::readUserInformation(const QDomElement& userElement)
{
  bool rc = true;
  signalProgress(0, 1, i18n("Loading user information..."));

  MyMoneyPayee user;
  user.setName(MyMoneyUtils::QStringEmpty(userElement.attribute(attributeName(Attribute::General::Name))));
  user.setEmail(MyMoneyUtils::QStringEmpty(userElement.attribute(attributeName(Attribute::General::Email))));

  QDomElement addressNode = findChildElement(elementName(Element::General::Address), userElement);
  if (!addressNode.isNull()) {
    user.setAddress(MyMoneyUtils::QStringEmpty(addressNode.attribute(attributeName(Attribute::General::Street))));
    user.setCity(MyMoneyUtils::QStringEmpty(addressNode.attribute(attributeName(Attribute::General::City))));
    user.setState(MyMoneyUtils::QStringEmpty(addressNode.attribute(attributeName(Attribute::General::Country))));
    user.setPostcode(MyMoneyUtils::QStringEmpty(addressNode.attribute(attributeName(Attribute::General::ZipCode))));
    user.setTelephone(MyMoneyUtils::QStringEmpty(addressNode.attribute(attributeName(Attribute::General::Telephone))));
  }

  m_storage->setUser(user);
  signalProgress(1, 0);

  return rc;
}

void MyMoneyStorageXML::writeInstitutions(QDomElement& institutions)
{
  const QList<MyMoneyInstitution> list = m_storage->institutionList();
  QList<MyMoneyInstitution>::ConstIterator it;
  institutions.setAttribute(attributeName(Attribute::General::Count), list.count());

  for (it = list.begin(); it != list.end(); ++it)
    writeInstitution(institutions, *it);
}

void MyMoneyStorageXML::writeInstitution(QDomElement& institution, const MyMoneyInstitution& i)
{
  MyMoneyXmlContentHandler::writeInstitution(i, *m_doc, institution);
}

void MyMoneyStorageXML::writePayees(QDomElement& payees)
{
  const QList<MyMoneyPayee> list = m_storage->payeeList();
  QList<MyMoneyPayee>::ConstIterator it;
  payees.setAttribute(attributeName(Attribute::General::Count), list.count());

  for (it = list.begin(); it != list.end(); ++it)
    writePayee(payees, *it);
}

void MyMoneyStorageXML::writePayee(QDomElement& payee, const MyMoneyPayee& p)
{
  MyMoneyXmlContentHandler::writePayee(p, *m_doc, payee);
}

void MyMoneyStorageXML::writeTags(QDomElement& tags)
{
  const QList<MyMoneyTag> list = m_storage->tagList();
  QList<MyMoneyTag>::ConstIterator it;
  tags.setAttribute(attributeName(Attribute::General::Count), list.count());

  for (it = list.begin(); it != list.end(); ++it)
    writeTag(tags, *it);
}

void MyMoneyStorageXML::writeTag(QDomElement& tag, const MyMoneyTag& ta)
{
  MyMoneyXmlContentHandler::writeTag(ta, *m_doc, tag);
}

void MyMoneyStorageXML::writeAccounts(QDomElement& accounts)
{
  QList<MyMoneyAccount> list;
  m_storage->accountList(list);
  QList<MyMoneyAccount>::ConstIterator it;
  accounts.setAttribute(attributeName(Attribute::General::Count), list.count() + 5);

  writeAccount(accounts, m_storage->asset());
  writeAccount(accounts, m_storage->liability());
  writeAccount(accounts, m_storage->expense());
  writeAccount(accounts, m_storage->income());
  writeAccount(accounts, m_storage->equity());

  signalProgress(0, list.count(), i18n("Saving accounts..."));
  int i = 0;
  for (it = list.constBegin(); it != list.constEnd(); ++it) {
    writeAccount(accounts, *it);
    signalProgress(++i, 0);
  }
}

void MyMoneyStorageXML::writeAccount(QDomElement& account, const MyMoneyAccount& p)
{
  MyMoneyXmlContentHandler::writeAccount(p, *m_doc, account);
}

void MyMoneyStorageXML::writeTransactions(QDomElement& transactions)
{
  MyMoneyTransactionFilter filter;
  filter.setReportAllSplits(false);
  const auto list = m_storage->transactionList(filter);
  transactions.setAttribute(attributeName(Attribute::General::Count), list.count());

  signalProgress(0, list.count(), i18n("Saving transactions..."));

  int i = 0;
  for (auto it = list.constBegin(); it != list.constEnd(); ++it) {
    writeTransaction(transactions, *it);
    signalProgress(++i, 0);
  }
}

void MyMoneyStorageXML::writeTransaction(QDomElement& transaction, const MyMoneyTransaction& tx)
{
  MyMoneyXmlContentHandler::writeTransaction(tx, *m_doc, transaction);
}

void MyMoneyStorageXML::writeSchedules(QDomElement& scheduled)
{
  const QList<MyMoneySchedule> list = m_storage->scheduleList(QString(), eMyMoney::Schedule::Type::Any, eMyMoney::Schedule::Occurrence::Any, eMyMoney::Schedule::PaymentType::Any,
                                                              QDate(), QDate(), false);
  QList<MyMoneySchedule>::ConstIterator it;
  scheduled.setAttribute(attributeName(Attribute::General::Count), list.count());

  for (it = list.constBegin(); it != list.constEnd(); ++it) {
    this->writeSchedule(scheduled, *it);
  }
}

void MyMoneyStorageXML::writeSchedule(QDomElement& scheduledTx, const MyMoneySchedule& tx)
{
  MyMoneyXmlContentHandler::writeSchedule(tx, *m_doc, scheduledTx);
}

void MyMoneyStorageXML::writeSecurities(QDomElement& equities)
{
  const QList<MyMoneySecurity> securityList = m_storage->securityList();
  equities.setAttribute(attributeName(Attribute::General::Count), securityList.count());
  if (securityList.size()) {
    for (QList<MyMoneySecurity>::ConstIterator it = securityList.constBegin(); it != securityList.constEnd(); ++it) {
      writeSecurity(equities, (*it));
    }
  }
}

void MyMoneyStorageXML::writeSecurity(QDomElement& securityElement, const MyMoneySecurity& security)
{
  MyMoneyXmlContentHandler::writeSecurity(security, *m_doc, securityElement);
}

void MyMoneyStorageXML::writeCurrencies(QDomElement& currencies)
{
  const QList<MyMoneySecurity> currencyList = m_storage->currencyList();
  currencies.setAttribute(attributeName(Attribute::General::Count), currencyList.count());
  if (currencyList.size()) {
    for (QList<MyMoneySecurity>::ConstIterator it = currencyList.constBegin(); it != currencyList.constEnd(); ++it) {
      writeSecurity(currencies, (*it));
    }
  }
}

void MyMoneyStorageXML::writeReports(QDomElement& parent)
{
  const QList<MyMoneyReport> list = m_storage->reportList();
  QList<MyMoneyReport>::ConstIterator it;
  parent.setAttribute(attributeName(Attribute::General::Count), list.count());

  signalProgress(0, list.count(), i18n("Saving reports..."));
  unsigned i = 0;
  for (it = list.constBegin(); it != list.constEnd(); ++it) {
    writeReport(parent, (*it));
    signalProgress(++i, 0);
  }
}

void MyMoneyStorageXML::writeReport(QDomElement& report, const MyMoneyReport& r)
{
  MyMoneyXmlContentHandler2::writeReport(r, *m_doc, report);
}

void MyMoneyStorageXML::writeBudgets(QDomElement& parent)
{
  const QList<MyMoneyBudget> list = m_storage->budgetList();
  QList<MyMoneyBudget>::ConstIterator it;
  parent.setAttribute(attributeName(Attribute::General::Count), list.count());

  signalProgress(0, list.count(), i18n("Saving budgets..."));
  unsigned i = 0;
  for (it = list.constBegin(); it != list.constEnd(); ++it) {
    writeBudget(parent, (*it));
    signalProgress(++i, 0);
  }
}

void MyMoneyStorageXML::writeBudget(QDomElement& budget, const MyMoneyBudget& b)
{
  MyMoneyXmlContentHandler2::writeBudget(b, *m_doc, budget);
}

void MyMoneyStorageXML::writeOnlineJobs(QDomElement& parent)
{
  const QList<onlineJob> list = m_storage->onlineJobList();
  parent.setAttribute(attributeName(Attribute::General::Count), list.count());
  signalProgress(0, list.count(), i18n("Saving online banking orders..."));
  unsigned i = 0;
  QList<onlineJob>::ConstIterator end = list.constEnd();
  for (QList<onlineJob>::ConstIterator it = list.constBegin(); it != end; ++it) {
    writeOnlineJob(parent, *it);
    signalProgress(++i, 0);
  }
}

void MyMoneyStorageXML::writeOnlineJob(QDomElement& onlineJobs, const onlineJob& job)
{
  MyMoneyXmlContentHandler::writeOnlineJob(job, *m_doc, onlineJobs);
}

void MyMoneyStorageXML::writeCostCenters(QDomElement& parent)
{
  const QList<MyMoneyCostCenter> list = m_storage->costCenterList();
  parent.setAttribute(attributeName(Attribute::General::Count), list.count());
  signalProgress(0, list.count(), i18n("Saving costcenters..."));
  unsigned i = 0;
  Q_FOREACH(MyMoneyCostCenter costCenter, list) {
    writeCostCenter(parent, costCenter);
    signalProgress(++i, 0);
  }
}

void MyMoneyStorageXML::writeCostCenter(QDomElement& costCenters, const MyMoneyCostCenter& costCenter)
{
  MyMoneyXmlContentHandler::writeCostCenter(costCenter, *m_doc, costCenters);
}

QDomElement MyMoneyStorageXML::findChildElement(const QString& name, const QDomElement& root)
{
  QDomNode child = root.firstChild();
  while (!child.isNull()) {
    if (child.isElement()) {
      QDomElement childElement = child.toElement();
      if (name == childElement.tagName()) {
        return childElement;
      }
    }

    child = child.nextSibling();
  }
  return QDomElement();
}

QDomElement MyMoneyStorageXML::writeKeyValuePairs(const QMap<QString, QString> pairs)
{
  if (m_doc) {
    QDomElement keyValPairs = m_doc->createElement(nodeName(Node::KeyValuePairs));

    QMap<QString, QString>::const_iterator it;
    for (it = pairs.constBegin(); it != pairs.constEnd(); ++it) {
      QDomElement pair = m_doc->createElement(elementName(Element::General::Pair));
      pair.setAttribute(attributeName(Attribute::General::Key), it.key());
      pair.setAttribute(attributeName(Attribute::General::Value), it.value());
      keyValPairs.appendChild(pair);
    }
    return keyValPairs;
  }
  return QDomElement();
}

void MyMoneyStorageXML::writePrices(QDomElement& prices)
{
  const MyMoneyPriceList list = m_storage->priceList();
  MyMoneyPriceList::ConstIterator it;
  prices.setAttribute(attributeName(Attribute::General::Count), list.count());

  for (it = list.constBegin(); it != list.constEnd(); ++it) {
    QDomElement price = m_doc->createElement(nodeName(Node::PricePair));
    price.setAttribute(attributeName(Attribute::General::From), it.key().first);
    price.setAttribute(attributeName(Attribute::General::To), it.key().second);
    writePricePair(price, *it);
    prices.appendChild(price);
  }
}

void MyMoneyStorageXML::writePricePair(QDomElement& price, const MyMoneyPriceEntries& p)
{
  MyMoneyPriceEntries::ConstIterator it;
  for (it = p.constBegin(); it != p.constEnd(); ++it) {
    QDomElement entry = m_doc->createElement(nodeName(Node::Price));
    writePrice(entry, *it);
    price.appendChild(entry);
  }
}

void MyMoneyStorageXML::writePrice(QDomElement& price, const MyMoneyPrice& p)
{
  price.setAttribute(attributeName(Attribute::General::Date), p.date().toString(Qt::ISODate));
  price.setAttribute(attributeName(Attribute::General::Price), p.rate(QString()).toString());
  price.setAttribute(attributeName(Attribute::General::Source), p.source());
}

void MyMoneyStorageXML::setProgressCallback(void(*callback)(int, int, const QString&))
{
  m_progressCallback = callback;
}

void MyMoneyStorageXML::signalProgress(int current, int total, const QString& msg)
{
  if (m_progressCallback != 0)
    (*m_progressCallback)(current, total, msg);
}

#if 0
/*!
    This convenience function returns all of the remaining data in the
    device.

    @note It's copied from the original Qt sources and modified to
          fix a problem with KFilterDev that does not correctly return
          atEnd() status in certain circumstances which caused our
          application to lock at startup.
*/
QByteArray QIODevice::readAll()
{
  if (isDirectAccess()) {
    // we know the size
    int n = size() - at(); // ### fix for 64-bit or large files?
    int totalRead = 0;
    QByteArray ba(n);
    char* c = ba.data();
    while (n) {
      int r = read(c, n);
      if (r < 0)
        return QByteArray();
      n -= r;
      c += r;
      totalRead += r;
      // If we have a translated file, then it is possible that
      // we read less bytes than size() reports
      if (atEnd()) {
        ba.resize(totalRead);
        break;
      }
    }
    return ba;
  } else {
    // read until we reach the end
    const int blocksize = 512;
    int nread = 0;
    QByteArray ba;
    int r = 1;
    while (!atEnd() && r != 0) {
      ba.resize(nread + blocksize);
      r = read(ba.data() + nread, blocksize);
      if (r < 0)
        return QByteArray();
      nread += r;
    }
    ba.resize(nread);
    return ba;
  }
}
#endif
