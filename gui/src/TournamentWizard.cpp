#include "TournamentWizard.h"

#include <QLabel>
#include <QVBoxLayout>

TournamentWizard::TournamentWizard(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("New Tournament");
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Tournament wizard coming soon.", this));
}
