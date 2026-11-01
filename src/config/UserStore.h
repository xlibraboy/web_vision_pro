#pragma once

#include <QString>
#include <QStringList>

#include <vector>

/**
 * Who is using the app. The values are ordered by privilege: a session at a
 * given role has every capability of the roles below it.
 *
 * Operator is the session the app opens in and needs no account - the
 * read-only day-to-day view (live trigger/snapshot, camera parameters
 * read-only, event review). Engineer and Administrator are stored accounts
 * (see UserStore) signed in with a password to unlock their tier.
 */
enum class AccessRole {
    Operator = 0,
    Engineer = 1,
    Administrator = 2
};

// True when role grants at least the capabilities of minimum.
inline bool hasAtLeast(AccessRole role, AccessRole minimum) {
    return static_cast<int>(role) >= static_cast<int>(minimum);
}

// Display name: "Operator", "Engineer", "Administrator".
QString accessRoleName(AccessRole role);

// A stored sign-in account. Operator sessions have no account.
struct UserAccount {
    QString username;
    AccessRole role = AccessRole::Engineer;
    QString salt;          // base64 of the per-account random salt
    QString passwordHash;  // base64 of the PBKDF2-HMAC-SHA256 digest
    int iterations = 0;    // PBKDF2 rounds the digest was derived with
};

/**
 * Sign-in accounts, kept in SystemConfig.ini beside the rest of the system
 * configuration. Passwords are never stored - only a salted
 * PBKDF2-HMAC-SHA256 digest - and an empty store seeds the first
 * Administrator (admin / admin) so a fresh installation can sign in and
 * manage accounts. Accounts only ever hold the Engineer or Administrator
 * role; Operator is not an account.
 */
class UserStore {
public:
    static constexpr int kMinPasswordLength = 4;
    static constexpr int kPbkdf2Iterations = 100000;
    static constexpr int kSaltBytes = 16;

    // All accounts, seeding the default Administrator when none is stored.
    static std::vector<UserAccount> users();

    // Account names only, in stored order (the login picker).
    static QStringList usernames();

    // Verifies a sign-in attempt; writes the account to *account when given.
    // An unknown user and a wrong password both fail.
    static bool verifyCredentials(const QString& username, const QString& password,
                                  UserAccount* account = nullptr);

    // Adds an account. Rejected - with *error set - on an empty or duplicate
    // (case-insensitive) user name, a password shorter than
    // kMinPasswordLength, or a role other than Engineer/Administrator.
    static bool addUser(const QString& username, AccessRole role, const QString& password,
                        QString* error = nullptr);

    // Updates an account's role and, when newPassword is not empty, its
    // password. The user name is the key and cannot change. Rejected when it
    // would strip the last Administrator.
    static bool updateUser(const QString& username, AccessRole role, const QString& newPassword,
                           QString* error = nullptr);

    // Removes an account. Rejected when it is the last Administrator.
    static bool removeUser(const QString& username, QString* error = nullptr);
};
