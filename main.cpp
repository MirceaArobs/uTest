#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QMessageBox>

int main(int argc, char *argv[]) {
    // Standard Qt Application setup
    QApplication app(argc, argv);

    // Create the main window container
    QWidget window;
    window.setWindowTitle("Framebuffer Qt Demo");
    window.resize(480, 320); // Common size for embedded SPI/HDMI displays

    // Create UI elements
    QListWidget *listWidget = new QListWidget(&window);
    listWidget->addItems({"Item 1: Initialize System", 
                          "Item 2: Calibrate Sensors", 
                          "Item 3: Run Diagnostic", 
                          "Item 4: Update Firmware",
                          "Item 5: Shut Down"});

    QPushButton *btnAction = new QPushButton("Process Item", &window);
    QPushButton *btnExit = new QPushButton("Exit", &window);

    // Style elements roughly for touch/low-res screens (optional but helpful for fb)
    btnAction->setMinimumHeight(40);
    btnExit->setMinimumHeight(40);

    // Set up layouts
    QVBoxLayout *mainLayout = new QVBoxLayout(&window);
    QHBoxLayout *buttonLayout = new QHBoxLayout();

    // Assemble the layouts
    mainLayout->addWidget(listWidget);
    buttonLayout->addWidget(btnAction);
    buttonLayout->addWidget(btnExit);
    mainLayout->addLayout(buttonLayout);

    // Connect signals and slots using lambda expressions
    QObject::connect(btnAction, &QPushButton::clicked, [&]() {
        QListWidgetItem *selected = listWidget->currentItem();
        if (selected) {
            QMessageBox::information(&window, "Action Triggered", 
                                     "Processing: " + selected->text());
        } else {
            QMessageBox::warning(&window, "Selection Missing", 
                                 "Please select an item from the list first.");
        }
    });

    QObject::connect(btnExit, &QPushButton::clicked, &app, &QApplication::quit);

    // Display the window
    window.show();

    return app.exec();
}

