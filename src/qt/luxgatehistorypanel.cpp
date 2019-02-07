#include "luxgatehistorypanel.h"
#include "ui_luxgatehistorypanel.h"

#include "luxgatehistorymodel.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "luxgatepricedelegate.h"
#include "luxgatehtmldelegate.h"

LuxgateHistoryPanel::LuxgateHistoryPanel(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::LuxgateHistoryPanel)
{
    ui->setupUi(this);

    ui->comboBoxRowsNum->addItems(QStringList() << "5" << "10" << "20"
                                                << "50" << "100" << "500");
    ui->comboBoxRowsNum->setCurrentText("500");
    connect(ui->comboBoxRowsNum, QOverload<const QString &>::of(&QComboBox::activated),
            this, [=](const QString &text){slotRowsDisplayChanged(text);});
}

void LuxgateHistoryPanel::slotRowsDisplayChanged (const QString &text)
{
    int rows = text.toInt();
    auto tableModel = qobject_cast<LuxgateHistoryModel *>(ui->tableViewHistory->model());
    tableModel->setRowsDisplayed(rows);
}

void LuxgateHistoryPanel::setModel(WalletModel *model)
{
    wal_model = model;
    OptionsModel * opt_model = wal_model->getOptionsModel();
    //init tableview
    {
        auto tableModel =new LuxgateHistoryModel(opt_model->getLuxgateDecimals(), this);
        ui->tableViewHistory->setModel(tableModel);
        QHeaderView * HeaderView = ui->tableViewHistory->horizontalHeader();
        HeaderView->setSectionResizeMode(QHeaderView::Stretch);
        HeaderView->setSectionsMovable(true);
        HeaderView->setSectionResizeMode(LuxgateHistoryModel::TickColumn, QHeaderView::Fixed);
        HeaderView->resizeSection(LuxgateHistoryModel::TickColumn, 25);
        HeaderView->setFixedHeight(30);
        connect(opt_model, &OptionsModel::luxgateDecimalsChanged,
                tableModel, &LuxgateHistoryModel::slotSetDecimals);

        ui->tableViewHistory->setItemDelegateForColumn(LuxgateHistoryModel::PriceColumn,
                                                        new LuxgatePriceDelegate(this));
        ui->tableViewHistory->setItemDelegateForColumn(LuxgateHistoryModel::TickColumn,
                                                       new LuxgateHtmlDelegate(this));
        ui->tableViewHistory->setItemDelegateForColumn(LuxgateHistoryModel::SideColumn,
                                                       new LuxgateHtmlDelegate(this));
        ui->tableViewHistory->setCopyColumns(QMap<int, int>{
                {LuxgateTableView::PriceColumn, LuxgateHistoryModel::PriceColumn},
                {LuxgateTableView::SizeColumn, LuxgateHistoryModel::SizeColumn},
                {LuxgateTableView::DateCloseOrderColumn, LuxgateHistoryModel::DateColumn}});
    }
}

LuxgateHistoryPanel::~LuxgateHistoryPanel()
{
    delete ui;
}