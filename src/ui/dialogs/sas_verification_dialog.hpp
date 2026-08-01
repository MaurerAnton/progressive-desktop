// src/ui/dialogs/sas_verification_dialog.hpp — SAS emoji comparison dialog.
#pragma once
#include <QDialog>
#include <QString>
#include <vector>
#include <string>

class QLabel;
class QPushButton;

namespace progressive::desktop {

struct VerificationEmoji;

class SasVerificationDialog : public QDialog {
    Q_OBJECT
public:
    SasVerificationDialog(const std::string& txnId, const std::string& otherUser,
                          const std::vector<VerificationEmoji>& emojis,
                          QWidget* parent = nullptr);

    const std::string& transactionId() const { return txnId_; }
    void setStatus(const QString& text);

signals:
    void matched();
    void mismatched();
    void cancelled();

private:
    std::string txnId_;
    QLabel* emojiLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* matchBtn_ = nullptr;
    QPushButton* noMatchBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
};

} // namespace progressive::desktop
