#include <QtCharts>            // Trae todo lo relacionado a QtCharts (gráficos)
#include <QtNetwork>           // Para QTcpServer, QTcpSocket y demás cosas de red
#include <QtWidgets>           // Para todos los widgets clásicos de Qt
#include <unordered_map>       // Uso de unordered_map para guardar bloques por dirección
#include <QtCharts/QChartView> // Vista para mostrar QChart
#include <QtCharts/QLineSeries>// Serie de línea para timeline
#include <algorithm>           // std::sort, std::max, etc.
#include <numeric>             // std::accumulate

// ------------------------------------------------------------------
// Estructura que representa la información que recibimos por red
// ------------------------------------------------------------------
// Aquí guardamos todo lo relevante de una asignación: dirección,
// tamaño, tipo, archivo/linea donde ocurrió, timestamp y si ya fue liberada.
struct MemoryBlock {
    QString address;    // Dirección (como string, la librería nos la envía así)
    qint64 size;        // Tamaño en bytes del bloque
    QString type;       // Tipo o etiqueta del bloque (opcional)
    QString file;       // Archivo origen de la asignación
    int line;           // Línea en el archivo
    qint64 timestamp;   // Momento de la asignación (epoch ms o s, consistente con la lib)
    bool freed = false; // Si ya se liberó ese bloque
};

// ------------------------------------------------------------------
// Ventana principal: UI + servidor TCP que recibe eventos JSON
// ------------------------------------------------------------------
class ProfilerWindow : public QMainWindow {
    Q_OBJECT
public:
    // Constructor: setea tamaño, UI y levanta el servidor
    ProfilerWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        resize(1200, 800); // ventana, para tablas y gráficos
        setupUi();         // construye pestañas, tablas, gráficos
        setupServer();     // inicia servidor TCP para recibir datos
    }

private:
    // -------------------------
    // Networking
    // -------------------------
    QTcpServer* server = nullptr;   // servidor que escucha conexiones entrantes
    QList<QTcpSocket*> clients;     // lista de clientes conectados (por si son varios)

    // -------------------------
    // Almacenamiento en memoria
    // -------------------------
    std::unordered_map<QString, MemoryBlock> blocks; // mapa dirección->MemoryBlock
    // se usa QString porque la instrumentación envía direcciones como string

    // -------------------------
    // Widgets y modelos UI
    // -------------------------
    QTabWidget* tabs; // control con pestañas principales

    // ----- Overview tab (vista general) -----
    QWidget* overviewTab;
    QLabel* lblUsageMB;       // etiqueta: uso actual en MB
    QLabel* lblActiveAllocs;  // etiqueta: conteo de asignaciones activas
    QLabel* lblLeaksMB;       // etiqueta: MB reportados como leaks
    QLabel* lblPeakMB;        // etiqueta: pico máximo de uso
    QLabel* lblTotalAllocs;   // etiqueta: total de asignaciones recibidas
    QChartView* timelineChartView; // vista que muestra el chart
    QLineSeries* timelineSeries;   // serie de datos (tiempo vs MB)
    QDateTimeAxis* axisX;     // eje X temporal
    QValueAxis* axisY;        // eje Y numérico (MB)
    QTableView* topFilesTable;      // tabla top files
    QStandardItemModel* topFilesModel; // modelo de la tabla top files

    // ----- Memory map tab -----
    QWidget* mapTab;
    QTableView* mapTable;
    QStandardItemModel* mapModel; // modelo que muestra cada bloque

    // ----- By source tab -----
    QWidget* bySourceTab;
    QTableView* sourceTable;
    QStandardItemModel* sourceModel; // modelo para agregados por archivo

    // ----- Leaks tab -----
    QWidget* leaksTab;
    QLabel* lblTotalLeaksMB;   // total fugado (MB acumulados por mensajes leak_report)
    QLabel* lblLargestLeak;    // leak más grande (bytes y dirección)
    QLabel* lblFileMostLeaks;  // archivo con más leaks reportados
    QLabel* lblLeakRate;       // tasa de leaks = leaks / totalAllocations
    QChartView* leaksBarChartView; // gráfico de barras por archivo
    QBarSeries* leaksBarSeries;    // serie de barras (se usa/actualiza)
    QChartView* leaksPieChartView; // gráfico de torta para distribución
    QLineSeries* leaksTimelineSeries; // serie temporal de leaks
    QChartView* leaksTimelineChartView; // vista del timeline de leaks

    // -------------------------
    // Agregados internos
    // -------------------------
    qreal currentUsageMB = 0;   // uso actual convertido a MB
    qreal peakUsageMB = 0;      // pico máximo observado
    qint64 totalAllocations = 0;// contador total de eventos de asignación
    qint64 totalLeakedMB = 0;   // MB acumulados reportados como fugas

    // -------------------------
    // Mapas auxiliares para agregados por archivo
    // -------------------------
    QMap<QString, qint64> allocsByFileBytes; // archivo -> bytes asignados (puede decrementar en liberaciones)
    QMap<QString, int> leaksByFileCount;     // archivo -> conteo de leaks reportados

    // ------------------------------------------------------------------
    // Construcción UI: crea pestañas y widgets. Nada de lógica de red aquí.
    // ------------------------------------------------------------------
    void setupUi() {
        tabs = new QTabWidget(this);  // pestañas principales
        setCentralWidget(tabs);       // ponemos el QTabWidget como centro de la ventana

        // separo la construcción de cada pestaña en funciones para que sea claro
        setupOverviewTab();
        setupMapTab();
        setupBySourceTab();
        setupLeaksTab();
    }

    // ------------------------------------------------------------------
    // Configura la pestaña "Vista general"
    // - métricas en la parte superior
    // - timeline (gráfico) en el medio
    // - tabla top files abajo
    // ------------------------------------------------------------------
    void setupOverviewTab() {
        overviewTab = new QWidget;
        QVBoxLayout* v = new QVBoxLayout(overviewTab); // layout vertical principal

        // Top metrics -> una fila con varias etiquetas
        QHBoxLayout* metricsLayout = new QHBoxLayout;
        lblUsageMB = new QLabel("Uso actual: 0 MB");             // muestra uso actual
        lblActiveAllocs = new QLabel("Asignaciones activas: 0"); // muestra # bloques en map
        lblLeaksMB = new QLabel("MB en leaks: 0");               // MB reportados como leaks
        lblPeakMB = new QLabel("Uso máximo: 0 MB");              // pico máximo observado
        lblTotalAllocs = new QLabel("Total asignaciones: 0");    // total de eventos asignación
        // agregar las etiquetas al layout de métricas
        metricsLayout->addWidget(lblUsageMB);
        metricsLayout->addWidget(lblActiveAllocs);
        metricsLayout->addWidget(lblLeaksMB);
        metricsLayout->addWidget(lblPeakMB);
        metricsLayout->addWidget(lblTotalAllocs);
        v->addLayout(metricsLayout); // añadir la fila de métricas al layout vertical

        // Timeline chart -> serie, ejes y QChartView para visualizar
        timelineSeries = new QLineSeries;          // serie donde vamos a insertar puntos (timestamp, MB)
        QChart* timelineChart = new QChart;        // chart contenedor
        timelineChart->addSeries(timelineSeries);  // añadimos la serie al chart

        axisX = new QDateTimeAxis;                 // eje X temporal
        axisX->setFormat("hh:mm:ss");              // formato visual de la hora
        axisX->setTitleText("Tiempo");             // título del eje X
        timelineChart->addAxis(axisX, Qt::AlignBottom);
        timelineSeries->attachAxis(axisX);         // asociar la serie al eje X

        axisY = new QValueAxis;                    // eje Y numérico
        axisY->setLabelFormat("%.2f");             // formato de 2 decimales
        axisY->setTitleText("MB");                 // título del eje Y
        timelineChart->addAxis(axisY, Qt::AlignLeft);
        timelineSeries->attachAxis(axisY);         // asociar la serie al eje Y

        timelineChart->legend()->hide();           // no necesitamos la leyenda en este chart
        timelineChart->setTitle("Uso de memoria (tiempo real)");
        timelineChartView = new QChartView(timelineChart);
        timelineChartView->setRenderHint(QPainter::Antialiasing); // suavizar líneas
        timelineChartView->setMinimumHeight(250); // que sea visible y no quede muy pequeño
        v->addWidget(timelineChartView); // añadir el chart a la pestaña

        // Top files table -> simple modelo con 3 columnas
        topFilesModel = new QStandardItemModel(0, 3, this);
        topFilesModel->setHeaderData(0, Qt::Horizontal, "Archivo");
        topFilesModel->setHeaderData(1, Qt::Horizontal, "Conteo");
        topFilesModel->setHeaderData(2, Qt::Horizontal, "MB");
        topFilesTable = new QTableView;
        topFilesTable->setModel(topFilesModel);
        topFilesTable->setMinimumHeight(200);
        v->addWidget(new QLabel("Top 3 archivos con mayores asignaciones"));
        v->addWidget(topFilesTable);

        tabs->addTab(overviewTab, "Vista general"); // añadir la pestaña al tabwidget
    }

    // ------------------------------------------------------------------
    // Configura la pestaña "Mapa de memoria"
    // - tabla con todos los bloques (dirección, tamaño, tipo, archivo, línea, estado)
    // ------------------------------------------------------------------
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
        // que las columnas se ajusten al ancho disponible
        mapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        v->addWidget(mapTable);
        tabs->addTab(mapTab, "Mapa de memoria");
    }

    // ------------------------------------------------------------------
    // Configura la pestaña "Asignación por archivo"
    // - muestra agregados por archivo (Bytes y conteo si se tuviera)
    // ------------------------------------------------------------------
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

    // ------------------------------------------------------------------
    // Configura la pestaña "Memory leaks"
    // - métricas de fugas, gráfico de barras, pie y timeline
    // ------------------------------------------------------------------
    void setupLeaksTab() {
        leaksTab = new QWidget;
        QVBoxLayout* v = new QVBoxLayout(leaksTab);

        // fila superior con labels resumidos de fugas
        QHBoxLayout* top = new QHBoxLayout;
        lblTotalLeaksMB = new QLabel("Total fugado: 0 MB");      // total MB reportados como fugas
        lblLargestLeak = new QLabel("Leak más grande: -");       // leak más grande encontrado
        lblFileMostLeaks = new QLabel("Archivo con más leaks: -"); // archivo con más leaks
        lblLeakRate = new QLabel("Tasa leaks: 0%");              // tasa = leaks / asignaciones totales
        top->addWidget(lblTotalLeaksMB);
        top->addWidget(lblLargestLeak);
        top->addWidget(lblFileMostLeaks);
        top->addWidget(lblLeakRate);
        v->addLayout(top);

        // Bar chart: leaks por archivo (conteo)
        leaksBarSeries = new QBarSeries;
        QChart* barChart = new QChart;
        barChart->addSeries(leaksBarSeries);
        barChart->setTitle("Leaks por archivo (conteo)");
        QChartView* barView = new QChartView(barChart);
        barView->setRenderHint(QPainter::Antialiasing);
        leaksBarChartView = barView;

        // Pie chart: distribución de leaks por archivo (porcentual)
        QChart* pieChart = new QChart;
        pieChart->setTitle("Distribución de leaks por archivo");
        leaksPieChartView = new QChartView(pieChart);
        leaksPieChartView->setRenderHint(QPainter::Antialiasing);

        // Timeline de leaks: serie de línea que podría mostrar número de leaks en el tiempo
        leaksTimelineSeries = new QLineSeries;
        QChart* timeline = new QChart;
        timeline->addSeries(leaksTimelineSeries);
        QDateTimeAxis* dtAxis = new QDateTimeAxis;
        dtAxis->setFormat("hh:mm:ss");
        timeline->addAxis(dtAxis, Qt::AlignBottom);
        leaksTimelineChartView = new QChartView(timeline);
        leaksTimelineChartView->setRenderHint(QPainter::Antialiasing);
        leaksTimelineChartView->setMinimumHeight(200);

        // Layout con las gráficas
        QHBoxLayout* charts = new QHBoxLayout;
        charts->addWidget(leaksBarChartView);
        charts->addWidget(leaksPieChartView);
        v->addLayout(charts);
        v->addWidget(leaksTimelineChartView);

        tabs->addTab(leaksTab, "Memory leaks");
    }

    // ------------------------------------------------------------------
    // Setup del servidor TCP
    // - escucha en puerto configurable y acepta conexiones
    // ------------------------------------------------------------------
    // crea el servidor y abre el puerto para recibir los mensajes
    void setupServer() {
        server = new QTcpServer(this);
        // cuando haya una nueva conexión entrante se llama a onNewConnection()
        connect(server, &QTcpServer::newConnection, this, &ProfilerWindow::onNewConnection);
        const quint16 port = 43210; // puerto por defecto, puede cambiarse
        if (!server->listen(QHostAddress::Any, port)) {
            // si no pudo iniciar, mostrar mensaje crítico y no seguir
            QMessageBox::critical(this, "Error", "No se pudo iniciar el servidor TCP en el puerto " + QString::number(port));
            return;
        }
        // mostrar en la barra de estado que estamos escuchando
        statusBar()->showMessage("Esperando conexiones en puerto " + QString::number(port));
    }

    // ------------------------------------------------------------------
    // Helpers para actualizar la UI desde los datos internos
    // ------------------------------------------------------------------
    void updateMetricsLabels() {
        // actualizar las etiquetas principales con los valores actuales
        lblUsageMB->setText(QString("Uso actual: %1 MB").arg(currentUsageMB, 0, 'f', 2));
        lblActiveAllocs->setText(QString("Asignaciones activas: %1").arg(blocks.size()));
        lblLeaksMB->setText(QString("MB en leaks: %1").arg(totalLeakedMB));
        lblPeakMB->setText(QString("Uso máximo: %1 MB").arg(peakUsageMB, 0, 'f', 2));
        lblTotalAllocs->setText(QString("Total asignaciones: %1").arg(totalAllocations));
    }

    void updateTopFiles() {
        // construye una lista (archivo, bytes) desde allocsByFileBytes y la ordena desc.
        QList<QPair<QString, qint64>> items;
        for (auto it = allocsByFileBytes.begin(); it != allocsByFileBytes.end(); ++it) {
            items.append({it.key(), it.value()});
        }
        // orden descendente por bytes (el que más bytes tenga primero)
        std::sort(items.begin(), items.end(), [](const QPair<QString,qint64>& a, const QPair<QString,qint64>& b){ return a.second > b.second; });
        // limpiar tabla y agregar top 3
        topFilesModel->removeRows(0, topFilesModel->rowCount());
        int limit = std::min(3, static_cast<int>(items.size()));
        for (int i=0;i<limit;i++) {
            QList<QStandardItem*> row;
            row << new QStandardItem(items[i].first); // nombre del archivo
            int count = 0; // no registramos conteo por archivo en este mapa simple (se puede mejorar)
            row << new QStandardItem(QString::number(count));
            // convertir bytes a MB con 2 decimales
            row << new QStandardItem(QString::number(items[i].second / 1024.0 / 1024.0, 'f', 2));
            topFilesModel->appendRow(row);
        }
    }

    void updateMapModel() {
        // vacía y rellena el modelo de la tabla con todos los bloques conocidos
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
        // similar a updateTopFiles pero rellena la tabla completa de archivos
        sourceModel->removeRows(0, sourceModel->rowCount());
        QList<QPair<QString, qint64>> items;
        for (auto it = allocsByFileBytes.begin(); it != allocsByFileBytes.end(); ++it) items.append({it.key(), it.value()});
        std::sort(items.begin(), items.end(), [](const QPair<QString,qint64>& a, const QPair<QString,qint64>& b){ return a.second > b.second; });
        for (auto &p : items) {
            QList<QStandardItem*> row;
            row << new QStandardItem(p.first);
            int count = 0; // no llevamos conteos aquí
            row << new QStandardItem(QString::number(count));
            row << new QStandardItem(QString::number(p.second / 1024.0 / 1024.0, 'f', 2));
            sourceModel->appendRow(row);
        }
    }

    void updateLeaksCharts() {
        // Actualiza labels básicos relacionados con fugas
        lblTotalLeaksMB->setText(QString("Total fugado: %1 MB").arg(totalLeakedMB));
        // buscar leak más grande (entre bloques no liberados)
        qint64 largest = 0; QString largestAddr;
        QString fileMost;
        int fileMostCount = 0;
        for (auto &kv : blocks) {
            const MemoryBlock& mb = kv.second;
            // si el bloque no está liberado y es más grande que el actual, actualizar
            if (!mb.freed && mb.size > largest) {
                largest = mb.size; largestAddr = mb.address; }
        }
        // buscar el archivo con más leaks reportados (según leaksByFileCount)
        for (auto it = leaksByFileCount.begin(); it != leaksByFileCount.end(); ++it) {
            if (it.value() > fileMostCount) { fileMostCount = it.value(); fileMost = it.key(); }
        }
        // actualizar etiquetas con la info
        lblLargestLeak->setText(largest > 0 ? QString("Leak más grande: %1 bytes (%2)").arg(largest).arg(largestAddr) : "Leak más grande: -");
        lblFileMostLeaks->setText(!fileMost.isEmpty() ? QString("Archivo con más leaks: %1 (%2)").arg(fileMost).arg(fileMostCount) : "Archivo con más leaks: -");
        // calcular tasa de leaks = total leaks / total asignaciones
        double leakRate = 0.0;
        if (totalAllocations > 0) {
            auto values = leaksByFileCount.values(); // QList<int> con conteos
            int sum = std::accumulate(values.begin(), values.end(), 0);
            leakRate = (double)sum / (double)totalAllocations * 100.0;
        }
        lblLeakRate->setText(QString("Tasa leaks: %1 %").arg(leakRate, 0, 'f', 2));

        // Reconstrucción de gráficos con los datos actuales de leaksByFileCount
        QBarSet* set = new QBarSet("Leaks"); // creado por compatibilidad, se usa barset luego
        QStringList categories;
        QList<qint64> counts;
        for (auto it = leaksByFileCount.begin(); it != leaksByFileCount.end(); ++it) {
            categories << it.key();
            counts << it.value();
        }
        if (categories.isEmpty()) {
            // si no hay categorías, limpiar chart actual (evita crash)
            QChart* c = leaksBarChartView->chart();
            c->removeAllSeries();
        } else {
            // construir series y barset con los counts
            QBarSeries* series = new QBarSeries;
            QBarSet* barset = new QBarSet("Leaks");
            for (qint64 v : counts) *barset << (double)v; // insertar valores en el barset
            series->append(barset);
            QChart* chart = new QChart;
            chart->addSeries(series);
            QBarCategoryAxis* axis = new QBarCategoryAxis;
            axis->append(categories);
            chart->createDefaultAxes();
            chart->setAxisX(axis, series);
            chart->setTitle("Leaks por archivo (conteo)");
            leaksBarChartView->setChart(chart);

            // Construir pie chart con las mismas categorías
            QPieSeries* pie = new QPieSeries;
            for (int i=0;i<categories.size();++i) pie->append(categories[i], counts[i]);
            QChart* piec = new QChart;
            piec->addSeries(pie);
            piec->setTitle("Distribución de leaks");
            leaksPieChartView->setChart(piec);
        }
    }

private slots:
    // ------------------------------------------------------------------
    // Slot: nuevo cliente conectado al servidor
    // - acepta todas las conexiones pendientes y conecta señales necesarias
    // ------------------------------------------------------------------
    void onNewConnection() {
        while (server->hasPendingConnections()) {
            QTcpSocket* sock = server->nextPendingConnection(); // aceptar cliente
            clients.append(sock); // guardar socket para poder cerrarlo luego si es necesario
            // conectar señal readyRead para cuando el cliente envíe datos
            connect(sock, &QTcpSocket::readyRead, this, &ProfilerWindow::onClientReadyRead);
            // conectar señal disconnected para limpiar cuando se desconecta
            connect(sock, &QTcpSocket::disconnected, this, &ProfilerWindow::onClientDisconnected);
            // mostrar en la barra de estado la IP del cliente que se conectó
            statusBar()->showMessage(QString("Cliente conectado: %1").arg(sock->peerAddress().toString()));
        }
    }

    // ------------------------------------------------------------------
    // Slot: cliente desconectado
    // - limpiar socket y mostrar mensaje
    // ------------------------------------------------------------------
    void onClientDisconnected() {
        QTcpSocket* s = qobject_cast<QTcpSocket*>(sender()); // socket que emitió la señal
        clients.removeAll(s); // quitar de la lista de clientes
        s->deleteLater();     // marcar para borrado de Qt (seguro)
        statusBar()->showMessage("Cliente desconectado");
    }

    // ------------------------------------------------------------------
    // Slot: cuando un cliente tiene datos listos para leer
    // - asumimos que cada mensaje JSON llega en una línea (terminado en \n)
    // ------------------------------------------------------------------
    void onClientReadyRead() {
        QTcpSocket* s = qobject_cast<QTcpSocket*>(sender());
        // leer por líneas porque la instrumentación envía JSON por línea
        while (s->canReadLine()) {
            QByteArray line = s->readLine().trimmed(); // obtener la línea y quitar espacios
            if (line.isEmpty()) continue; // ignorar líneas vacías
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(line, &err); // parsear JSON
            if (err.error != QJsonParseError::NoError) {
                // si el JSON está mal formado, avisar por consola y seguir con la siguiente línea
                qWarning() << "JSON parse error:" << err.errorString() << "line:" << line;
                continue;
            }
            if (!doc.isObject()) continue; // si no es un objeto JSON válido, ignorar
            QJsonObject obj = doc.object();
            processMessage(obj); // procesar el mensaje JSON (ver función abajo)
        }
    }

    // ------------------------------------------------------------------
    // Procesa el objeto JSON recibido
    // Tipos de eventos esperados:
    // - "asignacion": nueva asignación
    // - "liberacion": liberación de bloque
    // - "snapshot": estado puntual enviado por la librería
    // - "leak_report": reporte explícito de fuga detectada
    // ------------------------------------------------------------------
    void processMessage(const QJsonObject& obj) {
        QString evento = obj.value("evento").toString();

        // ------------------------------------------------------------------
        // Evento: asignacion
        // Recibimos dirección, tamaño, archivo, línea, tipo, timestamp
        // Guardamos un MemoryBlock en el mapa y actualizamos agregados.
        // ------------------------------------------------------------------
        if (evento == "asignacion") {
            QString addr = obj.value("direccion").toString();
            qint64 size = obj.value("tamano").toVariant().toLongLong();
            QString file = obj.value("archivo").toString();
            int line = obj.value("linea").toInt();
            QString type = obj.value("tipo").toString();
            qint64 ts = obj.value("timestamp").toVariant().toLongLong();
            // construir el bloque con lo que venga en el JSON
            MemoryBlock mb;
            mb.address = addr; mb.size = size; mb.file = file; mb.line = line; mb.type = type; mb.timestamp = ts; mb.freed = false;
            blocks[addr] = mb; // insertar/actualizar en el mapa por dirección

            // update aggregates -> convertir bytes a MB y actualizar estadísticas
            currentUsageMB += (double)size / (1024.0*1024.0); // aumentar uso actual
            peakUsageMB = std::max(peakUsageMB, currentUsageMB); // actualizar pico si aplica
            totalAllocations++; // contar esta asignación
            allocsByFileBytes[file] += size; // acumular bytes por archivo

            // update charts: append point to timeline (timestamp actual en ms)
            qint64 nowms = QDateTime::currentMSecsSinceEpoch();
            timelineSeries->append(nowms, currentUsageMB); // insertar punto (x=ms, y=MB)
            // mantener ventana de X de 60s (deslizante)
            axisX->setRange(QDateTime::fromMSecsSinceEpoch(nowms - 60000), QDateTime::fromMSecsSinceEpoch(nowms));
            // ajustar eje Y para que la gráfica se vea bien (mínimo razonable o 120% del uso)
            axisY->setRange(0, std::max( (qreal)10.0, currentUsageMB*1.2));

            // UI updates -> refrescar etiquetas y tablas
            updateMetricsLabels();
            updateTopFiles();
            updateMapModel();
            updateSourceModel();

        // ------------------------------------------------------------------
        // Evento: liberacion
        // - marcamos el bloque como liberado si existe y actualizamos métricas
        // ------------------------------------------------------------------
        } else if (evento == "liberacion") {
            QString addr = obj.value("direccion").toString();
            qint64 ts = obj.value("timestamp").toVariant().toLongLong();
            auto it = blocks.find(addr);
            if (it != blocks.end()) {
                MemoryBlock& mb = it->second;
                if (!mb.freed) {
                    mb.freed = true; // marcar liberado
                    currentUsageMB -= (double)mb.size / (1024.0*1024.0); // restar del uso actual
                    // nota: decidimos restar del acumulado por archivo para mantener estado "actual"
                    allocsByFileBytes[mb.file] -= mb.size;
                }
            }
            // añadir punto al timeline para reflejar la bajada de uso
            timelineSeries->append(QDateTime::currentMSecsSinceEpoch(), currentUsageMB);
            updateMetricsLabels();
            updateMapModel();
            updateSourceModel();

        // ------------------------------------------------------------------
        // Evento: snapshot
        // - la librería puede enviar un snapshot puntual con uso_actual_mb, uso_max_mb y total_asignaciones
        // ------------------------------------------------------------------
        } else if (evento == "snapshot") {
            double uso = obj.value("uso_actual_mb").toDouble();
            double peak = obj.value("uso_max_mb").toDouble();
            qint64 total = obj.value("total_asignaciones").toVariant().toLongLong();
            currentUsageMB = uso;
            peakUsageMB = std::max(peakUsageMB, peak);
            totalAllocations = total;
            timelineSeries->append(QDateTime::currentMSecsSinceEpoch(), currentUsageMB);
            updateMetricsLabels();
            updateTopFiles();

        // ------------------------------------------------------------------
        // Evento: leak_report
        // - mensaje opcional desde la instrumentación detectando una fuga
        // - actualiza totalLeakedMB y el conteo por archivo
        // ------------------------------------------------------------------
        } else if (evento == "leak_report") {
            QString addr = obj.value("direccion").toString();
            qint64 size = obj.value("tamano").toVariant().toLongLong();
            QString file = obj.value("archivo").toString();
            // suma MB fugados (convertir bytes a MB)
            totalLeakedMB += size / (1024.0*1024.0);
            leaksByFileCount[file]++; // incrementar conteo de leaks por archivo
            updateLeaksCharts();      // refrescar gráficos de leaks
            updateMetricsLabels();    // actualizar etiquetas
        }
    }
};

// ------------------------------------------------------------------
// main: inicializa QApplication y muestra la ventana ProfilerWindow
// ------------------------------------------------------------------
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Register QMetaType for QLineSeries axis plotting with ms
    ProfilerWindow w;
    w.show();
    return app.exec();
}
#include "main.moc"
