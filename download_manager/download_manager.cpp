#include "download_manager.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>

namespace dm{

namespace net{

namespace{

class recycle
{
public:
    explicit recycle(QNetworkReply *value) :
        value_(value) {}
    ~recycle(){ value_->deleteLater(); }

private:
    QNetworkReply *value_;
};

}

download_manager::download_manager(QObject *obj) :
    QObject(obj),
    manager_{new QNetworkAccessManager(obj)},
    max_download_size_{4},
    total_download_files_{0},
    uuid_{0}
{

}

int_fast64_t download_manager::
append(QUrl const &value,
       QString const &save_at,
       QString const &save_as)
{
    return start_download_impl(value, save_at, save_as);
}

bool download_manager::start_download(int_fast64_t uuid)
{
    qDebug()<<__func__<<"start download id "<<uuid;
    auto &id_set = download_info_.get<uid>();
    auto id_it = id_set.find(uuid);
    if(id_it != std::end(id_set)){
        qDebug()<<__func__<<" can find uuid";
        bool const success = id_set.modify(id_it, [&](auto &v)
        {
            v.error_.clear();
            QNetworkRequest request(v.url_);
            v.reply_ = manager_->get(request);
        });
        if(success){
            qDebug()<<__func__<<" can start download";
            connect_network_reply(id_it->reply_);
            return true;
        }
    }

    return false;
}

size_t download_manager::get_max_download_size() const
{
    return max_download_size_;
}

size_t download_manager::get_total_download_file() const
{
    return total_download_files_;
}

void download_manager::set_max_download_size(size_t value)
{
    max_download_size_ = value;
}

void download_manager::connect_network_reply(QNetworkReply *reply)
{
    connect(reply, SIGNAL(downloadProgress(qint64,qint64)),
            this, SLOT(download_progress(qint64,qint64)));
    connect(reply, SIGNAL(finished()),
            this, SLOT(download_finished()));
    connect(reply, SIGNAL(readyRead()),
            this, SLOT(download_ready_read()));
    connect(reply, SIGNAL(error(QNetworkReply::NetworkError)),
            this, SLOT(error(QNetworkReply::NetworkError)));
    ++total_download_files_;
}

int_fast64_t download_manager::start_download_impl(QUrl const &url,
                                                   QString const &save_at,
                                                   QString const &save_as)
{         
    QNetworkRequest request(url);
    QNetworkReply *reply = nullptr;
    if(total_download_files_ < max_download_size_){
        reply = manager_->get(request);
    }
    auto &uid_index = download_info_.get<uid>();
    download_info info{uuid_, reply, save_at, save_as};
    info.url_ = url;
    auto pair = uid_index.insert(info);
    if(pair.second){
        if(create_dir(save_at, uid_index, pair)){
            if(create_file(save_at, save_as, uid_index, pair)){
                if(reply){
                    connect_network_reply(reply);
                }
            }
        }
    }

    return uuid_++;
}

void download_manager::download_finished()
{    
    auto *reply = qobject_cast<QNetworkReply*>(sender());
    if(reply){
        recycle rc(reply);

        auto &net_index = download_info_.get<net_reply>();
        auto it = net_index.find(reply);
        if(it != std::end(net_index)){            
            emit download_finished(it->uuid_, it->error_);
            emit downloading_size_decrease(--total_download_files_);
        }
    }else{
        qDebug()<<__func__<<" : do not exist";
    }
}

void download_manager::
download_progress(qint64 bytes_received,
                  qint64 bytes_total)
{
    auto *reply = qobject_cast<QNetworkReply*>(sender());
    if(reply){
        auto &net_index = download_info_.get<net_reply>();
        auto it = net_index.find(reply);
        qDebug()<<__func__<< " receive "<<bytes_received;
        qDebug()<<__func__<< " total "<<bytes_total;
        if(it != std::end(net_index)){
            emit download_progress(it->uuid_, bytes_received,
                                   bytes_total);
        }
    }
}

void download_manager::download_ready_read()
{
    auto *reply = qobject_cast<QNetworkReply*>(sender());
    if(reply){
        qDebug()<<__func__<<" ready read";
        auto &net_index = download_info_.get<net_reply>();
        auto it = net_index.find(reply);
        if(it != std::end(net_index)){
            QByteArray data(reply->bytesAvailable(), Qt::Uninitialized);
            reply->read(data.data(), data.size());
            it->file_->write(data);

            emit download_ready_read(it->uuid_);
        }
    }else{
        qDebug()<<__func__<< " cannot cast sender to reply";
    }
}

void download_manager::error(QNetworkReply::NetworkError)
{
    auto *reply = qobject_cast<QNetworkReply*>(sender());
    if(reply){
        qDebug()<<__func__<<" : "<<reply->errorString();                
        auto &reply_set = download_info_.get<net_reply>();
        auto r_it = reply_set.find(reply);
        if(r_it != std::end(reply_set)){
            reply_set.modify(r_it, [&](auto &v)
            {
                v.error_ = reply->errorString();
            });
        }
    }
}

}

}
