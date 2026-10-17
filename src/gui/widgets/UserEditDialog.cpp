#include "UserEditDialog.h"

#include "../../config/CameraConfig.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

UserEditDialog::UserEditDialog(const QString& username, AccessRole role, bool isNew, QWidget* parent)
    : QDialog(parent)
    , isNew_(isNew)
    , username_(username)
    , role_(role) {
    setWindowTitle(isNew_ ? "Add User" : "Edit User");
    setModal(true);
    setFixedWidth(360);

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(10);

    QLabel* titleLabel = new QLabel(isNew_ ? "Add User" : "Edit User", this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleLabel->setFont(titleFont);
    root->addWidget(titleLabel);

    QLabel* subtitleLabel = new QLabel(
        "Engineer and Administrator accounts sign in from the status-bar pill "
        "to lift the Operator session.", this);
    subtitleLabel->setWordWrap(true);
    root->addWidget(subtitleLabel);
    root->addSpacing(4);

    QFormLayout* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    usernameEdit_ = new QLineEdit(username_, this);
    usernameEdit_->setPlaceholderText("e.g. j.smith");
    if (!isNew_) {
        // The user name is the store key; accounts are renamed by adding a new
        // one, not by editing the old one out of existence.
        usernameEdit_->setReadOnly(true);
    }
    form->addRow("User name:", usernameEdit_);

    roleCombo_ = new QComboBox(this);
    roleCombo_->addItem(accessRoleName(AccessRole::Engineer), static_cast<int>(AccessRole::Engineer));
    roleCombo_->addItem(accessRoleName(AccessRole::Administrator), static_cast<int>(AccessRole::Administrator));
    const int roleIndex = roleCombo_->findData(static_cast<int>(role_));
    roleCombo_->setCurrentIndex(roleIndex >= 0 ? roleIndex : 0);
    form->addRow("Role:", roleCombo_);

    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(isNew_ ? "Enter password..."
                                             : "Leave blank to keep the current password");
    form->addRow("Password:", passwordEdit_);

    confirmEdit_ = new QLineEdit(this);
    confirmEdit_->setEchoMode(QLineEdit::Password);
    confirmEdit_->setPlaceholderText("Repeat password...");
    form->addRow("Confirm:", confirmEdit_);

    showPasswordCheck_ = new QCheckBox("Show passwords", this);
    connect(showPasswordCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        const QLineEdit::EchoMode mode = checked ? QLineEdit::Normal : QLineEdit::Password;
        passwordEdit_->setEchoMode(mode);
        confirmEdit_->setEchoMode(mode);
    });
    root->addWidget(showPasswordCheck_);

    root->addSpacing(4);

    errorLabel_ = new QLabel(this);
    errorLabel_->setVisible(false);
    errorLabel_->setWordWrap(true);
    errorLabel_->setStyleSheet(QString("color: %1;").arg(CameraConfig::getThemeColors().critical));
    root->addWidget(errorLabel_);

    root->addStretch();

    QDialogButtonBox* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(isNew_ ? "Add User" : "Save");
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttonBox);

    usernameEdit_->setFocus();
}

void UserEditDialog::accept() {
    errorLabel_->setVisible(false);

    const QString name = usernameEdit_->text().trimmed();
    if (name.isEmpty()) {
        showError(QStringLiteral("Enter a user name."));
        return;
    }

    const QString password = passwordEdit_->text();
    const QString confirm = confirmEdit_->text();
    const bool passwordProvided = !password.isEmpty();
    const bool passwordRequired = isNew_ || passwordProvided;

    if (passwordRequired && password.length() < UserStore::kMinPasswordLength) {
        showError(QStringLiteral("The password needs at least %1 characters.")
                      .arg(UserStore::kMinPasswordLength));
        return;
    }
    if (passwordProvided && password != confirm) {
        showError(QStringLiteral("The passwords do not match."));
        return;
    }

    const AccessRole role = static_cast<AccessRole>(roleCombo_->currentData().toInt());

    QString storeError;
    const bool ok = isNew_
        ? UserStore::addUser(name, role, password, &storeError)
        : UserStore::updateUser(name, role, password, &storeError);
    if (!ok) {
        showError(storeError);
        return;
    }

    username_ = name;
    role_ = role;
    QDialog::accept();
}

void UserEditDialog::showError(const QString& message) {
    errorLabel_->setText(message);
    errorLabel_->setVisible(true);
}
