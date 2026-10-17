#include "UserStore.h"

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSettings>

#include <algorithm>

namespace {

constexpr const char* kDefaultAdminUsername = "admin";
constexpr const char* kDefaultAdminPassword = "admin";

constexpr int kDigestBytes = 32;

std::vector<UserAccount> readUsers() {
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "PaperVision", "SystemConfig");
    std::vector<UserAccount> accounts;
    const int count = settings.beginReadArray("Users");
    accounts.reserve(static_cast<size_t>(qMax(0, count)));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        UserAccount account;
        account.username = settings.value("username").toString().trimmed();
        const int role = settings.value("role", static_cast<int>(AccessRole::Engineer)).toInt();
        account.role = role == static_cast<int>(AccessRole::Administrator)
            ? AccessRole::Administrator
            : AccessRole::Engineer;
        account.salt = settings.value("salt").toString();
        account.passwordHash = settings.value("hash").toString();
        account.iterations = settings.value("iterations", 0).toInt();
        // A half-written record cannot be signed in with; skip it instead of
        // failing the whole store.
        if (!account.username.isEmpty() && !account.salt.isEmpty()
            && !account.passwordHash.isEmpty() && account.iterations > 0) {
            accounts.push_back(account);
        }
    }
    settings.endArray();
    return accounts;
}

void writeUsers(const std::vector<UserAccount>& accounts) {
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "PaperVision", "SystemConfig");
    settings.beginWriteArray("Users", static_cast<int>(accounts.size()));
    for (int i = 0; i < static_cast<int>(accounts.size()); ++i) {
        settings.setArrayIndex(i);
        const UserAccount& account = accounts[static_cast<size_t>(i)];
        settings.setValue("username", account.username);
        settings.setValue("role", static_cast<int>(account.role));
        settings.setValue("salt", account.salt);
        settings.setValue("hash", account.passwordHash);
        settings.setValue("iterations", account.iterations);
    }
    settings.endArray();
}

int indexOfUser(const std::vector<UserAccount>& accounts, const QString& username) {
    const QString name = username.trimmed();
    for (size_t i = 0; i < accounts.size(); ++i) {
        if (accounts[i].username.compare(name, Qt::CaseInsensitive) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int administratorCount(const std::vector<UserAccount>& accounts) {
    int count = 0;
    for (const UserAccount& account : accounts) {
        if (account.role == AccessRole::Administrator) {
            ++count;
        }
    }
    return count;
}

QByteArray makeSalt() {
    QByteArray salt;
    salt.reserve(UserStore::kSaltBytes);
    while (salt.size() < UserStore::kSaltBytes) {
        const quint32 word = QRandomGenerator::system()->generate();
        salt.append(reinterpret_cast<const char*>(&word), sizeof(word));
    }
    return salt;
}

QByteArray deriveDigest(const QString& password, const QByteArray& salt, int iterations) {
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
                                              password.toUtf8(), salt, iterations, kDigestBytes);
}

// Length-independent byte comparison: a wrong guess must not be distinguishable
// from a wrong-length one by how early the loop exits.
bool constantTimeEquals(const QByteArray& left, const QByteArray& right) {
    if (left.size() != right.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (int i = 0; i < left.size(); ++i) {
        difference |= static_cast<unsigned char>(left[i]) ^ static_cast<unsigned char>(right[i]);
    }
    return difference == 0;
}

void assignPassword(UserAccount& account, const QString& password) {
    const QByteArray salt = makeSalt();
    account.salt = QString::fromLatin1(salt.toBase64());
    account.iterations = UserStore::kPbkdf2Iterations;
    account.passwordHash = QString::fromLatin1(deriveDigest(password, salt, account.iterations).toBase64());
}

void setError(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
}

bool isStorableRole(AccessRole role) {
    return role == AccessRole::Engineer || role == AccessRole::Administrator;
}

} // namespace

QString accessRoleName(AccessRole role) {
    switch (role) {
        case AccessRole::Operator:
            return QStringLiteral("Operator");
        case AccessRole::Engineer:
            return QStringLiteral("Engineer");
        case AccessRole::Administrator:
            return QStringLiteral("Administrator");
    }
    return QStringLiteral("Operator");
}

std::vector<UserAccount> UserStore::users() {
    std::vector<UserAccount> accounts = readUsers();
    if (accounts.empty()) {
        // Fresh installation: seed the first Administrator so the configuration
        // (and the account list itself) can be unlocked and managed.
        UserAccount admin;
        admin.username = QString::fromLatin1(kDefaultAdminUsername);
        admin.role = AccessRole::Administrator;
        assignPassword(admin, QString::fromLatin1(kDefaultAdminPassword));
        accounts.push_back(admin);
        writeUsers(accounts);
    }
    return accounts;
}

QStringList UserStore::usernames() {
    QStringList names;
    for (const UserAccount& account : users()) {
        names.append(account.username);
    }
    return names;
}

bool UserStore::verifyCredentials(const QString& username, const QString& password, UserAccount* account) {
    const std::vector<UserAccount> accounts = users();
    const int index = indexOfUser(accounts, username);
    if (index < 0) {
        return false;
    }
    const UserAccount& stored = accounts[static_cast<size_t>(index)];
    const QByteArray salt = QByteArray::fromBase64(stored.salt.toLatin1());
    const QByteArray expected = QByteArray::fromBase64(stored.passwordHash.toLatin1());
    const QByteArray actual = deriveDigest(password, salt, stored.iterations);
    if (!constantTimeEquals(expected, actual)) {
        return false;
    }
    if (account) {
        *account = stored;
    }
    return true;
}

bool UserStore::addUser(const QString& username, AccessRole role, const QString& password, QString* error) {
    setError(error, QString());
    const QString name = username.trimmed();
    if (name.isEmpty()) {
        setError(error, QStringLiteral("Enter a user name."));
        return false;
    }
    if (!isStorableRole(role)) {
        setError(error, QStringLiteral("Accounts can only have the Engineer or Administrator role."));
        return false;
    }
    if (password.length() < kMinPasswordLength) {
        setError(error, QStringLiteral("The password needs at least %1 characters.").arg(kMinPasswordLength));
        return false;
    }

    std::vector<UserAccount> accounts = users();
    if (indexOfUser(accounts, name) >= 0) {
        setError(error, QStringLiteral("A user named \"%1\" already exists.").arg(name));
        return false;
    }

    UserAccount account;
    account.username = name;
    account.role = role;
    assignPassword(account, password);
    accounts.push_back(account);
    writeUsers(accounts);
    return true;
}

bool UserStore::updateUser(const QString& username, AccessRole role, const QString& newPassword, QString* error) {
    setError(error, QString());
    if (!isStorableRole(role)) {
        setError(error, QStringLiteral("Accounts can only have the Engineer or Administrator role."));
        return false;
    }
    if (!newPassword.isEmpty() && newPassword.length() < kMinPasswordLength) {
        setError(error, QStringLiteral("The password needs at least %1 characters.").arg(kMinPasswordLength));
        return false;
    }

    std::vector<UserAccount> accounts = users();
    const int index = indexOfUser(accounts, username);
    if (index < 0) {
        setError(error, QStringLiteral("Unknown user \"%1\".").arg(username.trimmed()));
        return false;
    }

    UserAccount& account = accounts[static_cast<size_t>(index)];
    if (account.role == AccessRole::Administrator && role != AccessRole::Administrator
        && administratorCount(accounts) <= 1) {
        setError(error, QStringLiteral("\"%1\" is the only Administrator account. "
                                       "Add another Administrator before changing its role.")
                           .arg(account.username));
        return false;
    }

    account.role = role;
    if (!newPassword.isEmpty()) {
        assignPassword(account, newPassword);
    }
    writeUsers(accounts);
    return true;
}

bool UserStore::removeUser(const QString& username, QString* error) {
    setError(error, QString());
    std::vector<UserAccount> accounts = users();
    const int index = indexOfUser(accounts, username);
    if (index < 0) {
        setError(error, QStringLiteral("Unknown user \"%1\".").arg(username.trimmed()));
        return false;
    }

    const UserAccount& account = accounts[static_cast<size_t>(index)];
    if (account.role == AccessRole::Administrator && administratorCount(accounts) <= 1) {
        setError(error, QStringLiteral("\"%1\" is the only Administrator account. "
                                       "Add another Administrator before removing it.")
                           .arg(account.username));
        return false;
    }

    accounts.erase(accounts.begin() + index);
    writeUsers(accounts);
    return true;
}
