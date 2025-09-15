/*
MemoryProfiler_GUI_qt.cpp
Single-file Qt5/Qt6 example application that implements the GUI for the Memory
Profiler project specified by the user.

Dependencies:
 - Qt 5.12+ or Qt 6
 - Qt Widgets
 - Qt Network (QTcpServer/QTcpSocket)
 - Qt Charts (for timeline, pie, bar charts)
 - CMake (recommended) or qmake

This file is a comprehensive starter implementation that provides:
 - A main window with the required tabs: Overview, Memory Map, By Source, Memory Leaks
 - A QTcpServer that listens for JSON messages from the instrumentation library
 - Data models (QStandardItemModel) to show tables and memory blocks
 - Charts for timeline, leaks distribution and bars using QtCharts
 - Hooks where the instrumentation JSON messages should be processed

Notes / Limitations:
 - This is a GUI-only implementation. The C++ instrumentation library must be
   implemented separately and send JSON messages (see sample messages below).
 - The code focuses on clarity and a real, runnable skeleton. You will likely
   add performance improvements for big traces (e.g., incremental model updates,
   virtual models, pagination).

Sample JSON messages expected from the instrumentation library (over TCP):

{"evento":"asignacion","direccion":"0x7ffee41b8","tamano":128,"archivo":"main.cpp","linea":42,"tipo":"int[]","timestamp":1694160000}

{"evento":"liberacion","direccion":"0x7ffee41b8","timestamp":1694160005}

{"evento":"snapshot","uso_actual_mb":12.5,"uso_max_mb":32.0,"total_asignaciones":1234,"timestamp":1694160010}

Send each JSON line terminated by "\n". The GUI expects UTF-8 JSON per line.

Build example (CMakeLists.txt snippet):
find_package(Qt6 COMPONENTS Widgets Network Charts REQUIRED)
add_executable(MemoryProfiler_GUI MemoryProfiler_GUI_qt.cpp)
target_link_libraries(MemoryProfiler_GUI Qt6::Widgets Qt6::Network Qt6::Charts)

*/

#include <QtCharts>
#include <QtNetwork>
#include <QtWidgets>
#include <unordered_map>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <algorithm>
#include <numeric>




struct MemoryBlock {
    QString address;
    qint64 size;
    QString type;
    QString file;
    int line;
    qint64 timestamp; // epoch seconds or ms (consistent with instrumentation)
    bool freed = false;
};

class ProfilerWindow : public QMainWindow {
    Q_OBJECT
public:
    ProfilerWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        resize(1200, 800);
        setupUi();
        setupServer();
    }

private:
    // Networking
    QTcpServer* server = nullptr;
    QList<QTcpSocket*> clients;

    // In-memory store
    std::unordered_map<QString, MemoryBlock> blocks; // key = address

    // Models and UI widgets
    QTabWidget* tabs;

    // Overview tab
    QWidget* overviewTab;
    QLabel* lblUsageMB;
    QLabel* lblActiveAllocs;
    QLabel* lblLeaksMB;
    QLabel* lblPeakMB;
    QLabel* lblTotalAllocs;
    QChartView* timelineChartView;
    QLineSeries* timelineSeries;
    QDateTimeAxis* axisX;
    QValueAxis* axisY;
    QTableView* topFilesTable;
    QStandardItemModel* topFilesModel;

    // Memory map tab
    QWidget* mapTab;
    QTableView* mapTable;
    QStandardItemModel* mapModel;

    // By source tab
    QWidget* bySourceTab;
    QTableView* sourceTable;
    QStandardItemModel* sourceModel;

    // Leaks tab
    QWidget* leaksTab;
    QLabel* lblTotalLeaksMB;
    QLabel* lblLargestLeak;
    QLabel* lblFileMostLeaks;
    QLabel* lblLeakRate;
    QChartView* leaksBarChartView;
    QBarSeries* leaksBarSeries;
    QChartView* leaksPieChartView;
    QLineSeries* leaksTimelineSeries;
    QChartView* leaksTimelineChartView;

    // Internal aggregates
    qreal currentUsageMB = 0;
    qreal peakUsageMB = 0;
    qint64 totalAllocations = 0;
    qint64 totalLeakedMB = 0;

    // Helper maps for aggregations
    QMap<QString, qint64> allocsByFileBytes; // file -> total bytes
    QMap<QString, int> leaksByFileCount; // file -> count

    void setupUi() {
        tabs = new QTabWidget(this);
        setCentralWidget(tabs);

        setupOverviewTab();
        setupMapTab();
        setupBySourceTab();
        setupLeaksTab();
    }

    void setupOverviewTab() {
        overviewTab = new QWidget;
        QVBoxLayout* v = new QVBoxLayout(overviewTab);

        // Top metrics
        QHBoxLayout* metricsLayout = new QHBoxLayout;
        lblUsageMB = new QLabel("Uso actual: 0 MB");
        lblActiveAllocs = new QLabel("Asignaciones activas: 0");
        lblLeaksMB = new QLabel("MB en leaks: 0");
        lblPeakMB = new QLabel("Uso máximo: 0 MB");
        lblTotalAllocs = new QLabel("Total asignaciones: 0");
        metricsLayout->addWidget(lblUsageMB);
        metricsLayout->addWidget(lblActiveAllocs);
        metricsLayout->addWidget(lblLeaksMB);
        metricsLayout->addWidget(lblPeakMB);
        metricsLayout->addWidget(lblTotalAllocs);
        v->addLayout(metricsLayout);

        // Timeline chart
        timelineSeries = new QLineSeries;
        QChart* timelineChart = new QChart;
        timelineChart->addSeries(timelineSeries);
        axisX = new QDateTimeAxis;
        axisX->setFormat("hh:mm:ss");
        axisX->setTitleText("Tiempo");
        timelineChart->addAxis(axisX, Qt::AlignBottom);
        timelineSeries->attachAxis(axisX);
        axisY = new QValueAxis;
        axisY->setLabelFormat("%.2f");
        axisY->setTitleText("MB");
        timelineChart->addAxis(axisY, Qt::AlignLeft);
        timelineSeries->attachAxis(axisY);
        timelineChart->legend()->hide();
        timelineChart->setTitle("Uso de memoria (tiempo real)");
        timelineChartView = new QChartView(timelineChart);
        timelineChartView->setRenderHint(QPainter::Antialiasing);
        timelineChartView->setMinimumHeight(250);
        v->addWidget(timelineChartView);

        // Top files table (bottom)
        topFilesModel = new QStandardItemModel(0, 3, this);
        topFilesModel->setHeaderData(0, Qt::Horizontal, "Archivo");
        topFilesModel->setHeaderData(1, Qt::Horizontal, "Conteo");
        topFilesModel->setHeaderData(2, Qt::Horizontal, "MB");
        topFilesTable = new QTableView;
        topFilesTable->setModel(topFilesModel);
        topFilesTable->setMinimumHeight(200);
        v->addWidget(new QLabel("Top 3 archivos con mayores asignaciones"));
        v->addWidget(topFilesTable);

        tabs->addTab(overviewTab, "Vista general");
    }

    void setupMapTab() {
        mapTab = new QWidget;
        QVBoxLayout* v = new QVBoxLayout(mapTab);
        mapModel = new QStandardItemModel(0, 6, this);
        mapModel->setHeaderData(0, Qt::Horizontal, "Dirección");
        mapModel->setHeaderData(1, Qt::Horizontal, "Tamaño (bytes)");
        mapModel->setHeaderData(2, Qt::Horizontal, "Tipo");
        mapModel->setHeaderData(3, Qt::Horizontal, "Archivo");
        mapModel->setHeaderData(4, Qt::Horizontal, "Línea");
        mapModel->setHeaderData(5, Qt::Horizontal, "Estado");
        mapTable = new QTableView;
        mapTable->setModel(mapModel);
        mapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        v->addWidget(mapTable);
        tabs->addTab(mapTab, "Mapa de memoria");
    }

    void setupBySourceTab() {
        bySourceTab = new QWidget;
        QVBoxLayout* v = new QVBoxLayout(bySourceTab);
        sourceModel = new QStandardItemModel(0, 3, this);
        sourceModel->setHeaderData(0, Qt::Horizontal, "Archivo");
        sourceModel->setHeaderData(1, Qt::Horizontal, "Conteo");
        sourceModel->setHeaderData(2, Qt::Horizontal, "MB");
        sourceTable = new QTableView;
        sourceTable->setModel(sourceModel);
        sourceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        v->addWidget(sourceTable);
        tabs->addTab(bySourceTab, "Asignación por archivo");
    }

    void setupLeaksTab() {
        leaksTab = new QWidget;
        QVBoxLayout* v = new QVBoxLayout(leaksTab);

        QHBoxLayout* top = new QHBoxLayout;
        lblTotalLeaksMB = new QLabel("Total fugado: 0 MB");
        lblLargestLeak = new QLabel("Leak más grande: -");
        lblFileMostLeaks = new QLabel("Archivo con más leaks: -");
        lblLeakRate = new QLabel("Tasa leaks: 0%");
        top->addWidget(lblTotalLeaksMB);
        top->addWidget(lblLargestLeak);
        top->addWidget(lblFileMostLeaks);
        top->addWidget(lblLeakRate);
        v->addLayout(top);

        // Bar chart for leaks by file
        leaksBarSeries = new QBarSeries;
        QChart* barChart = new QChart;
        barChart->addSeries(leaksBarSeries);
        barChart->setTitle("Leaks por archivo (conteo)");
        QChartView* barView = new QChartView(barChart);
        barView->setRenderHint(QPainter::Antialiasing);
        leaksBarChartView = barView;

        // Pie chart for leak distribution
        QChart* pieChart = new QChart;
        pieChart->setTitle("Distribución de leaks por archivo");
        leaksPieChartView = new QChartView(pieChart);
        leaksPieChartView->setRenderHint(QPainter::Antialiasing);

        // Timeline for leaks
        leaksTimelineSeries = new QLineSeries;
        QChart* timeline = new QChart;
        timeline->addSeries(leaksTimelineSeries);
        QDateTimeAxis* dtAxis = new QDateTimeAxis;
        dtAxis->setFormat("hh:mm:ss");
        timeline->addAxis(dtAxis, Qt::AlignBottom);
        leaksTimelineChartView = new QChartView(timeline);
        leaksTimelineChartView->setRenderHint(QPainter::Antialiasing);
        leaksTimelineChartView->setMinimumHeight(200);

        QHBoxLayout* charts = new QHBoxLayout;
        charts->addWidget(leaksBarChartView);
        charts->addWidget(leaksPieChartView);
        v->addLayout(charts);
        v->addWidget(leaksTimelineChartView);

        tabs->addTab(leaksTab, "Memory leaks");
    }

    void setupServer() {
        server = new QTcpServer(this);
        connect(server, &QTcpServer::newConnection, this, &ProfilerWindow::onNewConnection);
        const quint16 port = 43210; // configurable
        if (!server->listen(QHostAddress::Any, port)) {
            QMessageBox::critical(this, "Error", "No se pudo iniciar el servidor TCP en el puerto " + QString::number(port));
            return;
        }
        statusBar()->showMessage("Esperando conexiones en puerto " + QString::number(port));
    }

    // Update UI helpers
    void updateMetricsLabels() {
        lblUsageMB->setText(QString("Uso actual: %1 MB").arg(currentUsageMB, 0, 'f', 2));
        lblActiveAllocs->setText(QString("Asignaciones activas: %1").arg(blocks.size()));
        lblLeaksMB->setText(QString("MB en leaks: %1").arg(totalLeakedMB));
        lblPeakMB->setText(QString("Uso máximo: %1 MB").arg(peakUsageMB, 0, 'f', 2));
        lblTotalAllocs->setText(QString("Total asignaciones: %1").arg(totalAllocations));
    }

    void updateTopFiles() {
        // Create a vector of (file, bytes) and sort desc
        QList<QPair<QString, qint64>> items;
        for (auto it = allocsByFileBytes.begin(); it != allocsByFileBytes.end(); ++it) {
            items.append({it.key(), it.value()});
        }
        std::sort(items.begin(), items.end(), [](const QPair<QString,qint64>& a, const QPair<QString,qint64>& b){ return a.second > b.second; });
        topFilesModel->removeRows(0, topFilesModel->rowCount());
        int limit = std::min(3, static_cast<int>(items.size()));
        for (int i=0;i<limit;i++) {
            QList<QStandardItem*> row;
            row << new QStandardItem(items[i].first);
            int count = 0; // we don't track counts per-file in this map; could add easily
            row << new QStandardItem(QString::number(count));
            row << new QStandardItem(QString::number(items[i].second / 1024.0 / 1024.0, 'f', 2));
            topFilesModel->appendRow(row);
        }
    }

    void updateMapModel() {
        mapModel->removeRows(0, mapModel->rowCount());
        for (auto &kv : blocks) {
            const MemoryBlock& mb = kv.second;
            QList<QStandardItem*> row;
            row << new QStandardItem(mb.address);
            row << new QStandardItem(QString::number(mb.size));
            row << new QStandardItem(mb.type);
            row << new QStandardItem(mb.file);
            row << new QStandardItem(QString::number(mb.line));
            row << new QStandardItem(mb.freed ? "Liberado" : "Activo");
            mapModel->appendRow(row);
        }
    }

    void updateSourceModel() {
        sourceModel->removeRows(0, sourceModel->rowCount());
        QList<QPair<QString, qint64>> items;
        for (auto it = allocsByFileBytes.begin(); it != allocsByFileBytes.end(); ++it) items.append({it.key(), it.value()});
        std::sort(items.begin(), items.end(), [](const QPair<QString,qint64>& a, const QPair<QString,qint64>& b){ return a.second > b.second; });
        for (auto &p : items) {
            QList<QStandardItem*> row;
            row << new QStandardItem(p.first);
            int count = 0; // not tracked in this simple map
            row << new QStandardItem(QString::number(count));
            row << new QStandardItem(QString::number(p.second / 1024.0 / 1024.0, 'f', 2));
            sourceModel->appendRow(row);
        }
    }

    void updateLeaksCharts() {
        // Update labels
        lblTotalLeaksMB->setText(QString("Total fugado: %1 MB").arg(totalLeakedMB));
        // Largest leak and file with most leaks
        qint64 largest = 0; QString largestAddr;
        QString fileMost;
        int fileMostCount = 0;
        for (auto &kv : blocks) {
            const MemoryBlock& mb = kv.second;
            if (!mb.freed && mb.size > largest) {
                largest = mb.size; largestAddr = mb.address; }
        }
        // find file with most leaks
        for (auto it = leaksByFileCount.begin(); it != leaksByFileCount.end(); ++it) {
            if (it.value() > fileMostCount) { fileMostCount = it.value(); fileMost = it.key(); }
        }
        lblLargestLeak->setText(largest > 0 ? QString("Leak más grande: %1 bytes (%2)").arg(largest).arg(largestAddr) : "Leak más grande: -");
        lblFileMostLeaks->setText(!fileMost.isEmpty() ? QString("Archivo con más leaks: %1 (%2)").arg(fileMost).arg(fileMostCount) : "Archivo con más leaks: -");
        double leakRate = 0.0;
        if (totalAllocations > 0) {
            auto values = leaksByFileCount.values(); // devuelve QList<int>
            int sum = std::accumulate(values.begin(), values.end(), 0);
            leakRate = (double)sum / (double)totalAllocations * 100.0;
        }
        lblLeakRate->setText(QString("Tasa leaks: %1 %").arg(leakRate, 0, 'f', 2));

        // Bar chart rebuild
        QBarSet* set = new QBarSet("Leaks");
        QStringList categories;
        QList<qint64> counts;
        for (auto it = leaksByFileCount.begin(); it != leaksByFileCount.end(); ++it) {
            categories << it.key();
            counts << it.value();
        }
        if (categories.isEmpty()) {
            // clear
            QChart* c = leaksBarChartView->chart();
            c->removeAllSeries();
        } else {
            QBarSeries* series = new QBarSeries;
            QBarSet* barset = new QBarSet("Leaks");
            for (qint64 v : counts) *barset << (double)v;
            series->append(barset);
            QChart* chart = new QChart;
            chart->addSeries(series);
            QBarCategoryAxis* axis = new QBarCategoryAxis;
            axis->append(categories);
            chart->createDefaultAxes();
            chart->setAxisX(axis, series);
            chart->setTitle("Leaks por archivo (conteo)");
            leaksBarChartView->setChart(chart);

            // Pie
            QPieSeries* pie = new QPieSeries;
            for (int i=0;i<categories.size();++i) pie->append(categories[i], counts[i]);
            QChart* piec = new QChart;
            piec->addSeries(pie);
            piec->setTitle("Distribución de leaks");
            leaksPieChartView->setChart(piec);
        }
    }

private slots:
    void onNewConnection() {
        while (server->hasPendingConnections()) {
            QTcpSocket* sock = server->nextPendingConnection();
            clients.append(sock);
            connect(sock, &QTcpSocket::readyRead, this, &ProfilerWindow::onClientReadyRead);
            connect(sock, &QTcpSocket::disconnected, this, &ProfilerWindow::onClientDisconnected);
            statusBar()->showMessage(QString("Cliente conectado: %1").arg(sock->peerAddress().toString()));
        }
    }

    void onClientDisconnected() {
        QTcpSocket* s = qobject_cast<QTcpSocket*>(sender());
        clients.removeAll(s);
        s->deleteLater();
        statusBar()->showMessage("Cliente desconectado");
    }

    void onClientReadyRead() {
        QTcpSocket* s = qobject_cast<QTcpSocket*>(sender());
        while (s->canReadLine()) {
            QByteArray line = s->readLine().trimmed();
            if (line.isEmpty()) continue;
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(line, &err);
            if (err.error != QJsonParseError::NoError) {
                qWarning() << "JSON parse error:" << err.errorString() << "line:" << line;
                continue;
            }
            if (!doc.isObject()) continue;
            QJsonObject obj = doc.object();
            processMessage(obj);
        }
    }

    void processMessage(const QJsonObject& obj) {
        QString evento = obj.value("evento").toString();
        if (evento == "asignacion") {
            QString addr = obj.value("direccion").toString();
            qint64 size = obj.value("tamano").toVariant().toLongLong();
            QString file = obj.value("archivo").toString();
            int line = obj.value("linea").toInt();
            QString type = obj.value("tipo").toString();
            qint64 ts = obj.value("timestamp").toVariant().toLongLong();
            MemoryBlock mb;
            mb.address = addr; mb.size = size; mb.file = file; mb.line = line; mb.type = type; mb.timestamp = ts; mb.freed = false;
            blocks[addr] = mb;

            // update aggregates
            currentUsageMB += (double)size / (1024.0*1024.0);
            peakUsageMB = std::max(peakUsageMB, currentUsageMB);
            totalAllocations++;
            allocsByFileBytes[file] += size;

            // update charts: append point to timeline
            qint64 nowms = QDateTime::currentMSecsSinceEpoch();
            timelineSeries->append(nowms, currentUsageMB);
            axisX->setRange(QDateTime::fromMSecsSinceEpoch(nowms - 60000), QDateTime::fromMSecsSinceEpoch(nowms));
            axisY->setRange(0, std::max( (qreal)10.0, currentUsageMB*1.2));

            // UI updates
            updateMetricsLabels();
            updateTopFiles();
            updateMapModel();
            updateSourceModel();
        } else if (evento == "liberacion") {
            QString addr = obj.value("direccion").toString();
            qint64 ts = obj.value("timestamp").toVariant().toLongLong();
            auto it = blocks.find(addr);
            if (it != blocks.end()) {
                MemoryBlock& mb = it->second;
                if (!mb.freed) {
                    mb.freed = true;
                    currentUsageMB -= (double)mb.size / (1024.0*1024.0);
                    // update peak remains
                    // decrease allocsByFileBytes? keep history; for simplicity subtract
                    allocsByFileBytes[mb.file] -= mb.size;
                }
            }
            timelineSeries->append(QDateTime::currentMSecsSinceEpoch(), currentUsageMB);
            updateMetricsLabels();
            updateMapModel();
            updateSourceModel();
        } else if (evento == "snapshot") {
            // snapshot may include usage fields
            double uso = obj.value("uso_actual_mb").toDouble();
            double peak = obj.value("uso_max_mb").toDouble();
            qint64 total = obj.value("total_asignaciones").toVariant().toLongLong();
            currentUsageMB = uso; peakUsageMB = std::max(peakUsageMB, peak); totalAllocations = total;
            timelineSeries->append(QDateTime::currentMSecsSinceEpoch(), currentUsageMB);
            updateMetricsLabels();
            updateTopFiles();
        } else if (evento == "leak_report") {
            // optional special message reporting detected leak
            QString addr = obj.value("direccion").toString();
            qint64 size = obj.value("tamano").toVariant().toLongLong();
            QString file = obj.value("archivo").toString();
            totalLeakedMB += size / (1024.0*1024.0);
            leaksByFileCount[file]++;
            updateLeaksCharts();
            updateMetricsLabels();
        }
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Register QMetaType for QLineSeries axis plotting with ms
    ProfilerWindow w;
    w.show();
    return app.exec();
}
#include "main.moc"


