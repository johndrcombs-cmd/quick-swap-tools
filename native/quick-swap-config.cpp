#include <QApplication>

#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyCombination>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

#include <KGlobalAccel>
#include <KGlobalShortcutInfo>
#include <KKeySequenceRecorder>
#include <KKeySequenceWidget>

#include <array>
#include <iostream>
#include <utility>

namespace {
constexpr auto kComponent = "quick-swap-tools";

struct ShortcutAction {
    QString uniqueName;
    QString friendlyName;
    QKeySequence defaultSequence;
};

const ShortcutAction kAuction{
    QStringLiteral("next-auction"),
    QStringLiteral("Start next Whatnot auction"),
    QKeySequence(Qt::META | Qt::Key_A),
};
const ShortcutAction kGiveaway{
    QStringLiteral("next-giveaway"),
    QStringLiteral("Start next Whatnot giveaway"),
    QKeySequence(Qt::META | Qt::Key_G),
};

QStringList actionId(const ShortcutAction &action) {
    return {
        QString::fromLatin1(kComponent),
        action.uniqueName,
        QString::fromLatin1(kComponent),
        action.friendlyName,
    };
}

QKeySequence parseSequence(const QString &text) {
    if (text.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0
        || text.trimmed().isEmpty()) {
        return {};
    }
    return QKeySequence::fromString(text, QKeySequence::PortableText);
}

bool isRiskyUnmodifiedKey(const QKeySequence &sequence) {
    if (sequence.count() != 1) {
        return false;
    }
    const QKeyCombination combination = sequence[0];
    if (combination.keyboardModifiers() != Qt::NoModifier) {
        return false;
    }
    const int key = static_cast<int>(combination.key());
    return key < Qt::Key_F13 || key > Qt::Key_F24;
}

QJsonObject validateShortcuts(
    const QKeySequence &auction, const QKeySequence &giveaway) {
    QJsonObject result;
    QJsonArray warnings;
    if (auction.isEmpty() || giveaway.isEmpty()) {
        result.insert(QStringLiteral("valid"), false);
        result.insert(QStringLiteral("error"), QStringLiteral("both shortcuts are required"));
        result.insert(QStringLiteral("warnings"), warnings);
        return result;
    }
    if (auction == giveaway) {
        result.insert(QStringLiteral("valid"), false);
        result.insert(
            QStringLiteral("error"),
            QStringLiteral("auction and giveaway shortcuts must differ"));
        result.insert(QStringLiteral("warnings"), warnings);
        return result;
    }
    if (isRiskyUnmodifiedKey(auction)) {
        warnings.append(QStringLiteral("auction shortcut is an unmodified global key"));
    }
    if (isRiskyUnmodifiedKey(giveaway)) {
        warnings.append(QStringLiteral("giveaway shortcut is an unmodified global key"));
    }
    result.insert(QStringLiteral("valid"), true);
    result.insert(QStringLiteral("warnings"), warnings);
    return result;
}

enum class ShortcutSlot {
    Auction = 0,
    Giveaway = 1,
};

struct TransactionResult {
    bool applied = false;
    bool rollbackComplete = true;
    QString error;
};

template<typename Writer>
TransactionResult replaceShortcutsTransaction(
    Writer write,
    const QKeySequence &auction,
    const QKeySequence &giveaway,
    const QList<QKeySequence> &savedAuction,
    const QList<QKeySequence> &savedGiveaway) {
    TransactionResult result;
    QString operationError;
    result.applied =
        write(ShortcutSlot::Auction, {}, &operationError)
        && write(ShortcutSlot::Giveaway, {}, &operationError)
        && write(ShortcutSlot::Auction, {auction}, &operationError)
        && write(ShortcutSlot::Giveaway, {giveaway}, &operationError);
    if (result.applied) {
        return result;
    }

    QString auctionClearError;
    QString giveawayClearError;
    QString auctionRollbackError;
    QString giveawayRollbackError;
    const bool auctionCleared = write(
        ShortcutSlot::Auction, {}, &auctionClearError);
    const bool giveawayCleared = write(
        ShortcutSlot::Giveaway, {}, &giveawayClearError);
    const bool auctionRestored = write(
        ShortcutSlot::Auction, savedAuction, &auctionRollbackError);
    const bool giveawayRestored = write(
        ShortcutSlot::Giveaway, savedGiveaway, &giveawayRollbackError);
    result.rollbackComplete = auctionCleared && giveawayCleared
        && auctionRestored && giveawayRestored;
    result.error = operationError.isEmpty()
        ? QStringLiteral("KDE rejected the shortcut transaction.")
        : operationError;
    if (!result.rollbackComplete) {
        result.error += QStringLiteral(
            " Rollback was incomplete; open KDE Shortcuts to restore the mappings.");
        const QStringList rollbackErrors{
            auctionClearError,
            giveawayClearError,
            auctionRollbackError,
            giveawayRollbackError,
        };
        QString rollbackDetail;
        for (const QString &candidate : rollbackErrors) {
            if (!candidate.isEmpty()) {
                rollbackDetail = candidate;
                break;
            }
        }
        if (!rollbackDetail.isEmpty()) {
            result.error += QStringLiteral(" (%1)").arg(rollbackDetail);
        }
    }
    return result;
}

class ShortcutStore {
public:
    ShortcutStore()
        : interface_(
              QStringLiteral("org.kde.kglobalaccel"),
              QStringLiteral("/kglobalaccel"),
              QStringLiteral("org.kde.KGlobalAccel")) {
        qDBusRegisterMetaType<QKeySequence>();
        qDBusRegisterMetaType<QList<QKeySequence>>();
    }

    bool isAvailable() const {
        return interface_.isValid();
    }

    bool read(
        const ShortcutAction &action,
        QList<QKeySequence> *sequences,
        QString *error = nullptr) {
        QDBusReply<QList<QKeySequence>> reply = interface_.call(
            QStringLiteral("shortcutKeys"), actionId(action));
        if (!reply.isValid()) {
            if (error) {
                *error = reply.error().message();
            }
            return false;
        }
        *sequences = reply.value();
        return true;
    }

    bool writeAll(
        const ShortcutAction &action,
        const QList<QKeySequence> &sequences,
        QString *error = nullptr) {
        const QVariantList arguments{
            QVariant::fromValue(actionId(action)),
            QVariant::fromValue(sequences),
        };
        const QDBusMessage reply = interface_.callWithArgumentList(
            QDBus::Block, QStringLiteral("setForeignShortcutKeys"), arguments);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            if (error) {
                *error = reply.errorMessage();
            }
            return false;
        }
        QList<QKeySequence> stored;
        if (!read(action, &stored, error)) {
            return false;
        }
        return sameSequences(stored, sequences);
    }

    bool replacePair(
        const QKeySequence &auction,
        const QKeySequence &giveaway,
        const QList<QKeySequence> &savedAuction,
        const QList<QKeySequence> &savedGiveaway,
        QString *error) {
        const TransactionResult result = replaceShortcutsTransaction(
            [this](
                ShortcutSlot slot,
                const QList<QKeySequence> &sequences,
                QString *writeError) {
                return writeAll(
                    slot == ShortcutSlot::Auction ? kAuction : kGiveaway,
                    sequences,
                    writeError);
            },
            auction,
            giveaway,
            savedAuction,
            savedGiveaway);
        if (error) {
            *error = result.error;
        }
        return result.applied;
    }

private:
    static bool sameSequences(
        const QList<QKeySequence> &left,
        const QList<QKeySequence> &right) {
        if (left.size() != right.size()) {
            return false;
        }
        for (const QKeySequence &sequence : left) {
            if (!right.contains(sequence)) {
                return false;
            }
        }
        return true;
    }

    QDBusInterface interface_;
};

QString externalConflict(const QKeySequence &sequence) {
    const auto shortcuts = KGlobalAccel::globalShortcutsByKey(sequence);
    for (const KGlobalShortcutInfo &shortcut : shortcuts) {
        if (shortcut.componentUniqueName() == QString::fromLatin1(kComponent)
            && (shortcut.uniqueName() == kAuction.uniqueName
                || shortcut.uniqueName() == kGiveaway.uniqueName)) {
            continue;
        }
        return QStringLiteral("%1 — %2")
            .arg(shortcut.componentFriendlyName(), shortcut.friendlyName());
    }
    return {};
}

void configureRecorder(KKeySequenceWidget *widget) {
    widget->setMultiKeyShortcutsAllowed(false);
    widget->setPatterns(
        KKeySequenceRecorder::Key | KKeySequenceRecorder::ModifierAndKey);
    widget->setCheckForConflictsAgainst(KKeySequenceWidget::None);
    widget->setClearButtonShown(false);
    widget->setComponentName(QString::fromLatin1(kComponent));
}

int runValidationCli(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 4) {
        std::cerr << "usage: quick-swap-config --validate-shortcuts <auction> <giveaway>\n";
        return 64;
    }
    const QJsonObject validation = validateShortcuts(
        parseSequence(arguments[2]), parseSequence(arguments[3]));
    std::cout << QJsonDocument(validation).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return validation.value(QStringLiteral("valid")).toBool() ? 0 : 2;
}

int runTransactionSelfTest(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QList<QKeySequence> oldAuction{QKeySequence(Qt::Key_F20)};
    const QList<QKeySequence> oldGiveaway{QKeySequence(Qt::Key_F21)};
    const QKeySequence newAuction(Qt::Key_F21);
    const QKeySequence newGiveaway(Qt::Key_F20);

    auto run = [&](int failAfterWriteAt) {
        std::array<QList<QKeySequence>, 2> state{oldAuction, oldGiveaway};
        int callCount = 0;
        const TransactionResult result = replaceShortcutsTransaction(
            [&](ShortcutSlot slot, const QList<QKeySequence> &sequences, QString *error) {
                ++callCount;
                const int index = static_cast<int>(slot);
                const int other = index == 0 ? 1 : 0;
                if (!sequences.isEmpty()
                    && state[other].contains(sequences.first())) {
                    if (error) {
                        *error = QStringLiteral("conflicting sequence");
                    }
                    return false;
                }
                state[index] = sequences;
                if (callCount == failAfterWriteAt) {
                    if (error) {
                        *error = QStringLiteral("injected read-back failure");
                    }
                    return false;
                }
                return true;
            },
            newAuction,
            newGiveaway,
            oldAuction,
            oldGiveaway);
        return std::pair{result, state};
    };

    const auto [swapResult, swapState] = run(-1);
    const auto [rollbackResult, rollbackState] = run(4);
    const bool swapSucceeded = swapResult.applied
        && swapState[0] == QList<QKeySequence>{newAuction}
        && swapState[1] == QList<QKeySequence>{newGiveaway};
    const bool rollbackRestored = !rollbackResult.applied
        && rollbackResult.rollbackComplete
        && rollbackState[0] == oldAuction
        && rollbackState[1] == oldGiveaway;
    const QJsonObject output{
        {QStringLiteral("swapSucceeded"), swapSucceeded},
        {QStringLiteral("rollbackRestored"), rollbackRestored},
    };
    std::cout << QJsonDocument(output).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return swapSucceeded && rollbackRestored ? 0 : 1;
}

int runGui(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("quick-swap-config"));
    QCoreApplication::setOrganizationName(QStringLiteral("OniByts"));

    ShortcutStore store;
    if (!store.isAvailable()) {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Quick Swap Tools"),
            QStringLiteral("KDE Global Shortcuts is unavailable. Log into a KDE Plasma session and try again."));
        return 1;
    }

    QString readError;
    QList<QKeySequence> savedAuction;
    QList<QKeySequence> savedGiveaway;
    if (!store.read(kAuction, &savedAuction, &readError)
        || !store.read(kGiveaway, &savedGiveaway, &readError)) {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("Quick Swap Tools"),
            QStringLiteral("Could not read the current KDE shortcuts: %1").arg(readError));
        return 1;
    }
    const QKeySequence displayedAuction = savedAuction.isEmpty()
        ? kAuction.defaultSequence
        : savedAuction.first();
    const QKeySequence displayedGiveaway = savedGiveaway.isEmpty()
        ? kGiveaway.defaultSequence
        : savedGiveaway.first();

    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("Quick Swap Tools — Controls"));
    dialog.setMinimumWidth(560);

    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(QStringLiteral("<h2>Configure controls</h2>"));
    layout->addWidget(title);

    auto *instructions = new QLabel(QStringLiteral(
        "Click a binding, then press the keyboard, macro-pad, or controller button "
        "you want to use. Changes are not saved until you click <b>Apply</b>."));
    instructions->setWordWrap(true);
    layout->addWidget(instructions);

    auto *deviceNote = new QLabel(QStringLiteral(
        "<b>Controller note:</b> keyboard-style devices such as the Razer Tartarus "
        "work directly. A normal letter is not device-specific—it would also fire "
        "from your main keyboard. For gamepads, first map the button to an unused "
        "key such as F13–F24."));
    deviceNote->setWordWrap(true);
    deviceNote->setFrameStyle(QFrame::StyledPanel);
    deviceNote->setMargin(10);
    layout->addWidget(deviceNote);

    auto *form = new QFormLayout();
    auto *auctionWidget = new KKeySequenceWidget();
    auto *giveawayWidget = new KKeySequenceWidget();
    configureRecorder(auctionWidget);
    configureRecorder(giveawayWidget);
    auctionWidget->setKeySequence(displayedAuction);
    giveawayWidget->setKeySequence(displayedGiveaway);
    form->addRow(QStringLiteral("Next auction:"), auctionWidget);
    form->addRow(QStringLiteral("Next giveaway:"), giveawayWidget);
    layout->addLayout(form);

    auto *status = new QLabel(QStringLiteral("Current mappings loaded from KDE."));
    status->setWordWrap(true);
    layout->addWidget(status);

    auto *tools = new QHBoxLayout();
    auto *resetButton = new QPushButton(QStringLiteral("Reset to Super+A / Super+G"));
    auto *kdeButton = new QPushButton(QStringLiteral("Open KDE Shortcuts…"));
    tools->addWidget(resetButton);
    tools->addStretch();
    tools->addWidget(kdeButton);
    layout->addLayout(tools);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Close);
    layout->addWidget(buttons);

    QObject::connect(resetButton, &QPushButton::clicked, &dialog, [=] {
        auctionWidget->setKeySequence(kAuction.defaultSequence);
        giveawayWidget->setKeySequence(kGiveaway.defaultSequence);
        status->setText(QStringLiteral("Defaults selected. Click Apply to save them."));
    });
    QObject::connect(kdeButton, &QPushButton::clicked, &dialog, [] {
        QProcess::startDetached(QStringLiteral("kcmshell6"), {QStringLiteral("kcm_keys")});
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&] {
        const QKeySequence auction = auctionWidget->keySequence();
        const QKeySequence giveaway = giveawayWidget->keySequence();
        const QJsonObject validation = validateShortcuts(auction, giveaway);
        if (!validation.value(QStringLiteral("valid")).toBool()) {
            QMessageBox::warning(
                &dialog,
                QStringLiteral("Invalid controls"),
                validation.value(QStringLiteral("error")).toString());
            return;
        }

        const QJsonArray warnings = validation.value(QStringLiteral("warnings")).toArray();
        if (!warnings.isEmpty()) {
            const auto answer = QMessageBox::warning(
                &dialog,
                QStringLiteral("Unmodified global key"),
                QStringLiteral(
                    "At least one binding has no modifier. KDE cannot tell "
                    "your Tartarus from your main keyboard, so that key would trigger "
                    "Quick Swap Tools everywhere. Save it anyway?"),
                QMessageBox::Save | QMessageBox::Cancel,
                QMessageBox::Cancel);
            if (answer != QMessageBox::Save) {
                return;
            }
        }

        for (const auto &[name, sequence] : std::array{
                 std::pair{QStringLiteral("Next auction"), auction},
                 std::pair{QStringLiteral("Next giveaway"), giveaway},
             }) {
            const QString conflict = externalConflict(sequence);
            if (!conflict.isEmpty()) {
                QMessageBox::warning(
                    &dialog,
                    QStringLiteral("Shortcut already in use"),
                    QStringLiteral("%1 cannot use %2 because it belongs to %3.")
                        .arg(name, sequence.toString(QKeySequence::NativeText), conflict));
                return;
            }
        }

        QString error;
        if (!store.replacePair(
                auction, giveaway, savedAuction, savedGiveaway, &error)) {
            QMessageBox::critical(
                &dialog,
                QStringLiteral("Could not save controls"),
                error.isEmpty() ? QStringLiteral("KDE rejected the shortcut change.") : error);
            return;
        }
        savedAuction = {auction};
        savedGiveaway = {giveaway};
        status->setText(QStringLiteral("Saved. The new controls are active immediately."));
    });

    dialog.show();
    return app.exec();
}
} // namespace

int main(int argc, char **argv) {
    if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--validate-shortcuts")) {
        return runValidationCli(argc, argv);
    }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--self-test-transaction")) {
        return runTransactionSelfTest(argc, argv);
    }
    return runGui(argc, argv);
}
