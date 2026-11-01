#pragma once

#include <QObject>

// Regression tests for the sign-in account store behind the Operator /
// Engineer / Administrator roles.
//
// The store persists accounts in SystemConfig.ini and must seed a default
// Administrator on a fresh installation, verify only exact credentials
// (salted PBKDF2 - no stored passwords), refuse weak or duplicate accounts,
// and never let the last Administrator be removed or demoted.
class UserAccounts : public QObject {
    Q_OBJECT

private slots:
    void seededDefaultAdminSignsIn();
    void wrongPasswordAndUnknownUserAreRejected();
    void accountsSurviveSaveAndReload();
    void duplicateUsernameIsRejected();
    void shortPasswordIsRejected();
    void lastAdministratorCannotBeRemovedOrDemoted();
    void passwordChangeRoundTrip();
};
