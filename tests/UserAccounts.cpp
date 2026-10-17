#include "UserAccounts.h"

#include "config/UserStore.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {

int administratorCount()
{
    int count = 0;
    for (const UserAccount& account : UserStore::users()) {
        if (account.role == AccessRole::Administrator) {
            ++count;
        }
    }
    return count;
}

} // namespace

void UserAccounts::seededDefaultAdminSignsIn()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    UserAccount account;
    QVERIFY(UserStore::verifyCredentials(QStringLiteral("admin"), QStringLiteral("admin"), &account));
    QVERIFY(account.role == AccessRole::Administrator);
    QCOMPARE(account.username, QStringLiteral("admin"));
    QCOMPARE(administratorCount(), 1);
}

void UserAccounts::wrongPasswordAndUnknownUserAreRejected()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    UserStore::users(); // seed

    QVERIFY(!UserStore::verifyCredentials(QStringLiteral("admin"), QStringLiteral("wrong")));
    QVERIFY(!UserStore::verifyCredentials(QStringLiteral("admin"), QString()));
    QVERIFY(!UserStore::verifyCredentials(QStringLiteral("nobody"), QStringLiteral("admin")));
}

void UserAccounts::accountsSurviveSaveAndReload()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QString error;
    QVERIFY2(UserStore::addUser(QStringLiteral("eng1"), AccessRole::Engineer,
                                QStringLiteral("tuneit"), &error),
             qPrintable(error));

    // Re-read from disk (users() re-reads the store; the sign-in path).
    UserAccount account;
    QVERIFY(UserStore::verifyCredentials(QStringLiteral("eng1"), QStringLiteral("tuneit"), &account));
    QVERIFY(account.role == AccessRole::Engineer);
    QCOMPARE(static_cast<int>(UserStore::users().size()), 2);

    // The password itself must never be stored - only salt + digest.
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "PaperVision", "SystemConfig");
    const int count = settings.beginReadArray("Users");
    QCOMPARE(count, 2);
    bool foundEngineer = false;
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        if (settings.value("username").toString() == QStringLiteral("eng1")) {
            foundEngineer = true;
            QVERIFY(!settings.value("salt").toString().isEmpty());
            QVERIFY(!settings.value("hash").toString().isEmpty());
            QVERIFY(settings.value("iterations").toInt() > 0);
            QVERIFY(settings.value("hash").toString() != QStringLiteral("tuneit"));
        }
    }
    settings.endArray();
    QVERIFY(foundEngineer);
}

void UserAccounts::duplicateUsernameIsRejected()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    UserStore::users(); // seed admin

    QString error;
    QVERIFY(!UserStore::addUser(QStringLiteral("admin"), AccessRole::Engineer,
                                QStringLiteral("whatever"), &error));
    QVERIFY(!error.isEmpty());
    // Case-insensitive, and surrounding whitespace is trimmed.
    QVERIFY(!UserStore::addUser(QStringLiteral("ADMIN"), AccessRole::Engineer,
                                QStringLiteral("whatever"), &error));
    QVERIFY(!UserStore::addUser(QStringLiteral("  admin "), AccessRole::Engineer,
                                QStringLiteral("whatever"), &error));
    QCOMPARE(static_cast<int>(UserStore::users().size()), 1);
}

void UserAccounts::shortPasswordIsRejected()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    UserStore::users(); // seed admin

    QString error;
    QVERIFY(!UserStore::addUser(QStringLiteral("eng1"), AccessRole::Engineer,
                                QStringLiteral("abc"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!UserStore::addUser(QString(), AccessRole::Engineer,
                                QStringLiteral("longenough"), &error));
    // The Operator role is a session state, never a stored account.
    QVERIFY(!UserStore::addUser(QStringLiteral("op1"), AccessRole::Operator,
                                QStringLiteral("longenough"), &error));
    QCOMPARE(static_cast<int>(UserStore::users().size()), 1);
}

void UserAccounts::lastAdministratorCannotBeRemovedOrDemoted()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    UserStore::users(); // seed admin

    QString error;
    QVERIFY(!UserStore::removeUser(QStringLiteral("admin"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!UserStore::updateUser(QStringLiteral("admin"), AccessRole::Engineer, QString(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(UserStore::verifyCredentials(QStringLiteral("admin"), QStringLiteral("admin")));

    // With a second Administrator removal becomes legal (admin2 remains).
    QVERIFY2(UserStore::addUser(QStringLiteral("admin2"), AccessRole::Administrator,
                                QStringLiteral("second"), &error),
             qPrintable(error));
    QVERIFY2(UserStore::removeUser(QStringLiteral("admin"), &error), qPrintable(error));
    QCOMPARE(administratorCount(), 1);
    QVERIFY(!UserStore::verifyCredentials(QStringLiteral("admin"), QStringLiteral("admin")));

    // admin2 is the last one now: both operations are blocked again.
    QVERIFY(!UserStore::removeUser(QStringLiteral("admin2"), &error));
    QVERIFY(!UserStore::updateUser(QStringLiteral("admin2"), AccessRole::Engineer, QString(), &error));
}

void UserAccounts::passwordChangeRoundTrip()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QString error;
    QVERIFY2(UserStore::addUser(QStringLiteral("eng1"), AccessRole::Engineer,
                                QStringLiteral("firstpass"), &error),
             qPrintable(error));

    // An empty new password keeps the current one.
    QVERIFY2(UserStore::updateUser(QStringLiteral("eng1"), AccessRole::Administrator, QString(), &error),
             qPrintable(error));
    UserAccount account;
    QVERIFY(UserStore::verifyCredentials(QStringLiteral("eng1"), QStringLiteral("firstpass"), &account));
    QVERIFY(account.role == AccessRole::Administrator);

    // A new password replaces it; the old one stops working.
    QVERIFY2(UserStore::updateUser(QStringLiteral("eng1"), AccessRole::Administrator,
                                   QStringLiteral("secondpass"), &error),
             qPrintable(error));
    QVERIFY(!UserStore::verifyCredentials(QStringLiteral("eng1"), QStringLiteral("firstpass")));
    QVERIFY(UserStore::verifyCredentials(QStringLiteral("eng1"), QStringLiteral("secondpass"), &account));
    QVERIFY(account.role == AccessRole::Administrator);

    QVERIFY(!UserStore::updateUser(QStringLiteral("ghost"), AccessRole::Engineer, QString(), &error));
    QVERIFY(!error.isEmpty());
}
