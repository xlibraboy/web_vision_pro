#pragma once

#include <QDialog>

#include "../../config/UserStore.h"

class QLineEdit;
class QComboBox;
class QLabel;
class QCheckBox;

/**
 * Add or edit one sign-in account: user name, role and password.
 *
 * In edit mode the user name is fixed (it keys the stored record) and an empty
 * password keeps the current one. The dialog runs the UserStore operation
 * itself on accept - including the store's own guards (duplicate name, short
 * password, last Administrator) - and shows any failure inline while staying
 * open, so callers only have to check exec() == Accepted.
 */
class UserEditDialog : public QDialog {
    Q_OBJECT

public:
    // isNew = add a new account; otherwise edit the account named username.
    UserEditDialog(const QString& username, AccessRole role, bool isNew,
                   QWidget* parent = nullptr);

protected:
    void accept() override;

private:
    void showError(const QString& message);

    bool isNew_;
    QString username_;
    AccessRole role_;

    QLineEdit* usernameEdit_ = nullptr;
    QComboBox* roleCombo_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLineEdit* confirmEdit_ = nullptr;
    QCheckBox* showPasswordCheck_ = nullptr;
    QLabel* errorLabel_ = nullptr;
};
